#include "common.hpp"
#include "zygisk.hpp"
#include <android/log.h>
#include <sys/socket.h>
#include <sys/sysmacros.h>
#include <fcntl.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <limits.h>
#include <atomic>
static std::string xmlSnapshot,xmlHash,processName;
static FILE* (*originalFopen)(const char*,const char*)=nullptr;
static std::atomic<unsigned> reads{0};
static bool transfer(int fd,void* data,size_t n,bool sending){
    size_t pos=0;while(pos<n){ssize_t r=sending?send(fd,(char*)data+pos,n-pos,MSG_NOSIGNAL):recv(fd,(char*)data+pos,n-pos,0);if(r<0&&errno==EINTR)continue;if(r<=0)return false;pos+=r;}return true;
}
static FILE* readOverride(const char* path,const char* mode){
    bool readOnly=mode&&mode[0]=='r'&&strchr(mode,'+')==nullptr;
    if(path&&strcmp(path,LOW_PATH)==0&&readOnly&&!xmlSnapshot.empty()){
        FILE* f=fmemopen(xmlSnapshot.data(),xmlSnapshot.size(),"rb");
        if(f){if(reads.fetch_add(1)<3)__android_log_print(ANDROID_LOG_INFO,"MyronAudioControl","read_override process=%s pid=%d xml_sha256=%s bytes=%zu",processName.c_str(),getpid(),xmlHash.c_str(),xmlSnapshot.size());return f;}
        __android_log_print(ANDROID_LOG_WARN,"MyronAudioControl","read_override_failed process=%s errno=%d; using original",processName.c_str(),errno);
    }
    return originalFopen?originalFopen(path,mode):nullptr;
}
static void companion(int socket){
    try{
        const std::string base="/data/adb/modules/myron_audio_control";
        if(exists(base+"/disable")||exists(base+"/remove")||!exists(base+"/runtime/boot-ready"))throw std::runtime_error("module not active");
        if(readFile(base+"/runtime/boot-ready")!=readFile("/proc/sys/kernel/random/boot_id"))throw std::runtime_error("stale boot readiness");
        char canonical[PATH_MAX];if(!realpath((base+"/runtime/current").c_str(),canonical))throw std::runtime_error("no active snapshot");std::string path=canonical;
        if(path.rfind(base+"/runtime/gen-",0)!=0||path.substr((base+"/runtime/gen-").size()).find('/')!=std::string::npos)throw std::runtime_error("bad active path");
        auto config=Json::parse(readFile(path+"/config.json")),meta=Json::parse(readFile(path+"/meta.json"));std::string xml=readFile(path+"/audio_lowpower_app_list.xml");
        if(meta["xmlHash"]!=digest(xml))throw std::runtime_error("snapshot hash mismatch");
        bool unmountOnly=unmountOnlyPolicy();
        Json reply={{"ok",true},{"volumeUnity",config["volumeUnity"]},{"scope",meta["scope"]},{"xml",xml},{"xmlHash",meta["xmlHash"]},{"allowUnmountOnly",unmountOnly}};std::string message=reply.dump();uint32_t size=message.size();
        if(size>262144)return;transfer(socket,&size,sizeof size,true);transfer(socket,message.data(),size,true);
    }catch(...){uint32_t size=0;transfer(socket,&size,sizeof size,true);}
}
class AudioModule:public zygisk::ModuleBase{
    zygisk::Api* api=nullptr;
    bool enabled=false;
    bool onDenylist=false;
public:
    void onLoad(zygisk::Api* a,JNIEnv*)override{api=a;}
    void preAppSpecialize(zygisk::AppSpecializeArgs* args)override{
        const char* name=environment->GetStringUTFChars(args->nice_name,nullptr);if(!name){api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);return;}processName=name;environment->ReleaseStringUTFChars(args->nice_name,name);
        onDenylist=(api->getFlags()&zygisk::PROCESS_ON_DENYLIST)!=0;
        if(args->uid<10000){
            if(processName=="tv.danmaku.bili"||processName=="com.maxmpz.audioplayer")__android_log_print(ANDROID_LOG_INFO,"MyronAudioControl","not_covered process=%s uid=%d flags=%u reason=zygisk_exclusion_or_system_uid",processName.c_str(),args->uid,api->getFlags());
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);return;
        }
        int fd=api->connectCompanion();if(fd<0){__android_log_print(ANDROID_LOG_WARN,"MyronAudioControl","not_covered process=%s pid=%d reason=companion_unavailable",processName.c_str(),getpid());api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);return;}
        timeval timeout{2,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);uint32_t size=0;
        bool success=transfer(fd,&size,sizeof size,false)&&size>0&&size<=262144;std::string message;if(success){message.resize(size);success=transfer(fd,message.data(),size,false);}close(fd);
        try{
            if(!success)throw std::runtime_error("snapshot unavailable");auto data=Json::parse(message);if(!data.value("ok",false)||!data.value("volumeUnity",false))throw std::runtime_error("normalization disabled");
            if(onDenylist&&!data.value("allowUnmountOnly",false))throw std::runtime_error("denylist policy is enforced or unrecognized; original behavior retained");
            auto package=processName.substr(0,processName.find(':'));std::string scope=data.at("scope");
            if(scope=="test"&&package!="tv.danmaku.bili"&&package!="com.maxmpz.audioplayer"&&package!="com.smartviser.viserneo")throw std::runtime_error("outside test scope");
            if(scope!="test"&&scope!="all")throw std::runtime_error("invalid scope");xmlSnapshot=data.at("xml");xmlHash=data.at("xmlHash");if(digest(xmlSnapshot)!=xmlHash)throw std::runtime_error("bad snapshot digest");enabled=true;
            if(onDenylist)__android_log_print(ANDROID_LOG_INFO,"MyronAudioControl","policy_unmount_only process=%s; native read allowed, visible mounts not restored",processName.c_str());
        }catch(const std::exception& e){
            __android_log_print(ANDROID_LOG_INFO,"MyronAudioControl","not_covered process=%s pid=%d reason=%s",processName.c_str(),getpid(),e.what());
            xmlSnapshot.clear();api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
        }
    }
    void postAppSpecialize(const zygisk::AppSpecializeArgs*)override{
        if(!enabled)return;
        FILE* maps=fopen("/proc/self/maps","r");if(!maps){xmlSnapshot.clear();__android_log_print(ANDROID_LOG_WARN,"MyronAudioControl","hook_failed process=%s pid=%d reason=maps_unavailable",processName.c_str(),getpid());return;}char line[2048];bool found=false;
        while(fgets(line,sizeof line,maps)){
            unsigned majorId,minorId;unsigned long inode;char path[1024]{};
            if(sscanf(line,"%*s %*s %*s %x:%x %lu %1023s",&majorId,&minorId,&inode,path)==4&&strstr(path,"/libtinyxml2.so")){
                api->pltHookRegister(makedev(majorId,minorId),inode,"fopen",reinterpret_cast<void*>(readOverride),reinterpret_cast<void**>(&originalFopen));found=true;break;
            }
        }fclose(maps);
        if(!found||!api->pltHookCommit()||!originalFopen){xmlSnapshot.clear();__android_log_print(ANDROID_LOG_WARN,"MyronAudioControl","hook_failed process=%s pid=%d; original policy retained",processName.c_str(),getpid());return;}
        __android_log_print(ANDROID_LOG_INFO,"MyronAudioControl","hook_registered process=%s pid=%d xml_sha256=%s; playback read not yet verified",processName.c_str(),getpid(),xmlHash.c_str());
    }
private:
    JNIEnv* environment=nullptr;
public:
    void setEnvironment(JNIEnv* e){environment=e;}
};
class RegisteredAudioModule:public AudioModule{
public:void onLoad(zygisk::Api* a,JNIEnv* e)override{AudioModule::onLoad(a,e);setEnvironment(e);}
};
REGISTER_ZYGISK_MODULE(RegisteredAudioModule)
REGISTER_ZYGISK_COMPANION(companion)
