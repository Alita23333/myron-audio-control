#include "common.hpp"
#include "bass_engine.hpp"
#include <sound/asound.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/system_properties.h>
#include <poll.h>
#include <signal.h>
#include <fcntl.h>
#include <limits.h>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <atomic>
#include <iostream>
static std::string root;
static volatile sig_atomic_t stopping=0;
static void stop(int){stopping=1;}
static std::string boot(){return trim(readFile("/proc/sys/kernel/random/boot_id"));}
static void save(const std::string& name,const Json& j){
    auto path=root+"/runtime/"+name,tmp=path+".tmp-"+std::to_string(getpid());
    auto data=j.dump(2)+"\n";int fd=open(tmp.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600);
    if(fd<0)throw std::runtime_error("cannot write bass status");
    size_t pos=0;while(pos<data.size()){auto n=write(fd,data.data()+pos,data.size()-pos);if(n<=0){close(fd);throw std::runtime_error("short bass status write");}pos+=size_t(n);}
    close(fd);if(rename(tmp.c_str(),path.c_str()))throw std::runtime_error("bass status commit failed");
}
static unsigned regRead(unsigned device,unsigned reg){
    char base[128];snprintf(base,sizeof base,"/sys/bus/i2c/devices/13-%04x/",device);
    int fd=open((std::string(base)+"reg").c_str(),O_WRONLY|O_CLOEXEC);
    if(fd<0)throw std::runtime_error("TFA selector unavailable");
    unsigned char selector=reg;auto n=write(fd,&selector,1);close(fd);if(n!=1)throw std::runtime_error("TFA selector failed");
    fd=open((std::string(base)+"rw").c_str(),O_RDONLY|O_CLOEXEC);if(fd<0)throw std::runtime_error("TFA read unavailable");
    unsigned char b[2]{};n=read(fd,b,2);close(fd);if(n!=2)throw std::runtime_error("TFA read failed");return unsigned(b[0])*256+b[1];
}
static void lowWrite(unsigned word){
    unsigned char reg=0x5f,b[]={static_cast<unsigned char>(word>>8),static_cast<unsigned char>(word)};
    int fd=open("/sys/bus/i2c/devices/13-0034/reg",O_WRONLY|O_CLOEXEC);if(fd<0)throw std::runtime_error("TFA write selector unavailable");
    auto n=write(fd,&reg,1);close(fd);if(n!=1)throw std::runtime_error("TFA write selector failed");
    fd=open("/sys/bus/i2c/devices/13-0034/rw",O_WRONLY|O_CLOEXEC);if(fd<0)throw std::runtime_error("TFA writer unavailable");
    n=write(fd,b,2);close(fd);if(n!=2)throw std::runtime_error("TFA write failed");
}
static bool identity(){
    char device[PROP_VALUE_MAX]{};__system_property_get("ro.product.device",device);
    if(strcmp(device,"myron"))return false;
    for(auto d:{0x34u,0x35u,0x36u})
        if(regRead(d,3)!=0x9866||regRead(d,6)!=0x0507||((regRead(d,0x21)>>8)&15)!=(d==0x34?2:d==0x35?1:0))return false;
    return true;
}
static std::string profile(){
    int fd=open("/dev/snd/controlC0",O_RDONLY|O_CLOEXEC);if(fd<0)return {};
    snd_ctl_elem_value value{};value.id.iface=SNDRV_CTL_ELEM_IFACE_MIXER;strcpy(reinterpret_cast<char*>(value.id.name),"TFA Profile");
    if(ioctl(fd,SNDRV_CTL_IOCTL_ELEM_READ,&value)){close(fd);return {};}
    snd_ctl_elem_info info{};info.id=value.id;info.value.enumerated.item=value.value.enumerated.item[0];
    if(ioctl(fd,SNDRV_CTL_IOCTL_ELEM_INFO,&info)){close(fd);return {};}
    close(fd);return reinterpret_cast<char*>(info.value.enumerated.name);
}
static bool normalMode(){
    // No shell input or application data enters this fixed diagnostic command.
    FILE* f=popen("/system/bin/dumpsys -t 1 audio","r");if(!f)return false;
    char line[4096];bool section=false,normal=false;size_t size=0;
    while(fgets(line,sizeof line,f)){
        size+=strlen(line);if(size>2097152){normal=false;break;}
        auto text=trim(line);
        if(text=="## Audio mode:")section=true;
        else if(section&&text.rfind("- Actual mode = ",0)==0){normal=text=="- Actual mode = MODE_NORMAL";section=false;}
    }
    auto rc=pclose(f);return rc==0&&normal;
}
static bool restore(unsigned expected){
    if(!identity())return false;
    auto current=regRead(0x34,0x5f),code=bass::field(current);
    if(code==bass::factory||code!=expected)return true; // Another owner wins.
    lowWrite(bass::replace(current,bass::factory));return bass::field(regRead(0x34,0x5f))==bass::factory;
}
struct Recovery { std::atomic<unsigned> owned{0},expected{5}; };
static int selfTest(){
    int count=0;auto check=[&](bool v){if(!v)throw std::runtime_error("bass engine self-test failed");count++;};
    bass::Snapshot s{true,true,true,true,true,0x80a7};
    for(int level=0;level<=3;level++){
        bass::Engine e;auto action=e.tick(s,level);
        check(level?bass::field(unsigned(action))==5-unsigned(level):action==-1);
        if(level){check((unsigned(action)&~bass::mask)==(s.word&~bass::mask));auto boosted=s;boosted.word=unsigned(action);check(e.tick(boosted,level)==-1);
            boosted.normal=false;check(bass::field(unsigned(e.tick(boosted,level)))==5);check(!e.owned);}
    }
    bass::Engine e;e.tick(s,1);auto changed=s;changed.word=0x00c0;
    check(e.tick(changed,1)==-1&&!e.owned);check(e.tick(s,1)==-1);changed.active=false;e.tick(changed,1);check(e.tick(s,1)>=0);
    bass::Engine invalid;changed=s;changed.identity=false;check(invalid.tick(changed,3)==-1);changed=s;changed.stereo=false;check(invalid.tick(changed,3)==-1);
    for(auto bad:{-1,4}){bool rejected=false;try{bass::target(bad);}catch(...){rejected=true;}check(rejected);}
    std::cout<<Json{{"ok",true},{"checks",count}}.dump()<<std::endl;return 0;
}
static int run(){
    int lock=open((root+"/runtime/bass.lock").c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600);
    if(lock<0||flock(lock,LOCK_EX|LOCK_NB))throw std::runtime_error("bass service already running");
    auto config=Json::parse(readFile(root+"/runtime/current/config.json"));
    if(config.contains("bassBoost")&&!config["bassBoost"].is_number_integer())throw std::runtime_error("invalid bass level type");
    int level=config.value("bassBoost",0);bass::target(level);
    const auto bootId=boot();
    auto report=[&](const std::string& state,bool owned,const std::string& error=""){
        save("bass-status.json",{{"state",state},{"level",level},{"owned",owned},{"targetCode",bass::target(level)},{"factoryCode",5},{"pid",getpid()},{"bootId",bootId},{"error",error}});
    };
    if(!level){report("stock",false);close(lock);return 0;}
    if(exists(root+"/disable")||exists(root+"/remove")||trim(readFile(root+"/runtime/boot-ready"))!=bootId)throw std::runtime_error("module not ready");
    auto shared=static_cast<Recovery*>(mmap(nullptr,sizeof(Recovery),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0));
    if(shared==MAP_FAILED)throw std::runtime_error("watchdog memory failed");new(shared) Recovery;
    static_assert(std::atomic<unsigned>::is_always_lock_free);
    int pipefd[2];if(pipe(pipefd))throw std::runtime_error("watchdog pipe failed");
    auto child=fork();if(child<0)throw std::runtime_error("watchdog fork failed");
    if(child==0){
        close(pipefd[1]);setsid();signal(SIGHUP,SIG_IGN);signal(SIGTERM,SIG_IGN);signal(SIGINT,SIG_IGN);
        pollfd p{pipefd[0],POLLIN|POLLHUP,0};
        for(;;){int rc=poll(&p,1,8000);char b=0;if(rc>0&&read(pipefd[0],&b,1)==1){if(b=='D')_exit(0);continue;}break;}
        bool ok=true;try{if(shared->owned.load())ok=restore(shared->expected.load());report(ok?"watchdog_restored":"restore_failed",!ok,"service stopped responding");}catch(...){ok=false;}
        _exit(ok?0:2);
    }
    close(pipefd[0]);signal(SIGPIPE,SIG_IGN);signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGHUP,stop);
    bass::Engine engine;std::string lastState,lastError;int result=0;
    try{
        while(!stopping&&!exists(root+"/disable")&&!exists(root+"/remove")&&exists(root+"/module.prop")){
            bass::Snapshot s;s.identity=identity();
            if(!s.identity&&engine.owned)throw std::runtime_error("hardware identity changed while owned");
            // Slot configuration may not exist until the first speaker start.
            // Keep waiting without writing; never confuse this with ownership.
            if(s.identity){
                s.word=regRead(0x34,0x5f);s.active=(regRead(0x34,0x11)&0x0400)!=0;
                s.stereo=profile()=="stereo";s.gainMatch=true;
                for(auto d:{0x34u,0x35u,0x36u})if(((regRead(d,0x52)>>5)&255)!=160)s.gainMatch=false;
                // Query communication mode only when hardware could be boosted.
                s.normal=s.active&&s.stereo&&normalMode();
            }
            auto expected=engine.expected;auto action=engine.tick(s,level);
            if(action>=0){
                shared->expected.store(engine.owned?engine.expected:expected);shared->owned.store(1);
                save("bass-recovery.json",{{"bootId",bootId},{"expected",shared->expected.load()},{"factoryCode",5}});
                if(regRead(0x34,0x5f)!=s.word)throw std::runtime_error("register changed before update; stopped");
                lowWrite(unsigned(action));if(regRead(0x34,0x5f)!=unsigned(action))throw std::runtime_error("bass readback mismatch");
            }
            shared->expected.store(engine.expected);shared->owned.store(engine.owned?1:0);
            if(engine.state!=lastState){report(engine.state,engine.owned);lastState=engine.state;}
            const char heartbeat='H';if(write(pipefd[1],&heartbeat,1)!=1)throw std::runtime_error("watchdog disconnected");
            for(unsigned tick=0;tick<10&&!stopping;tick++)usleep(100000);
        }
    }catch(const std::exception& e){result=1;lastError=e.what();try{report("error",shared->owned.load()!=0,lastError);}catch(...){} }
    bool restored=true;try{if(shared->owned.load())restored=restore(shared->expected.load());}catch(...){restored=false;}
    if(restored){shared->owned.store(0);char done='D';write(pipefd[1],&done,1);if(result)report("error_restored",false,lastError);else if(engine.state!="unsupported_hardware")report("stopped_restored",false);}
    else{result=1;try{report("restore_failed",true);}catch(...){}}
    close(pipefd[1]);int childStatus=0;waitpid(child,&childStatus,0);munmap(shared,sizeof(Recovery));close(lock);
    return result;
}
int main(int argc,char** argv){
    try{
        if(argc==2&&std::string(argv[1])=="self-test")return selfTest();
        if(getuid()!=0||argc!=2)throw std::runtime_error("root and one command required");
        char path[PATH_MAX];if(!realpath(argv[0],path))throw std::runtime_error("backend path unavailable");root=path;root=root.substr(0,root.rfind('/'));root=root.substr(0,root.rfind('/'));
        std::string cmd=argv[1];
        if(cmd=="run")return run();
        if(cmd=="probe"){
            Json j={{"supported",identity()},{"profile",profile()},{"normalMode",normalMode()}};
            for(auto d:{0x34u,0x35u,0x36u}){char key[8];snprintf(key,sizeof key,"0x%02x",d);j[key]={{"gain",(regRead(d,0x52)>>5)&255},{"attenuation",bass::field(regRead(d,0x5f))},{"active",(regRead(d,0x11)&0x0400)!=0}};}
            std::cout<<j.dump()<<std::endl;return 0;
        }
        if(cmd=="restore"){
            if(exists(root+"/runtime/bass-status.json")){
                auto state=Json::parse(readFile(root+"/runtime/bass-status.json"));int pid=state.value("pid",0);
                if(pid>1&&pid!=getpid()){
                    char exe[PATH_MAX]{};auto n=readlink(("/proc/"+std::to_string(pid)+"/exe").c_str(),exe,sizeof(exe)-1);
                    if(n>0&&(std::string(exe)==root+"/bin/bassctl"||std::string(exe)==root+"/bin/bassctl (deleted)")){kill(pid,SIGTERM);for(int i=0;i<30&&kill(pid,0)==0;i++)usleep(100000);}
                }
            }
            if(exists(root+"/runtime/bass-recovery.json")){
                auto j=Json::parse(readFile(root+"/runtime/bass-recovery.json"));auto expected=j.at("expected").get<unsigned>();
                if(j["bootId"]!=boot()||j["factoryCode"]!=5||expected<2||expected>4)throw std::runtime_error("stale/invalid recovery record");
                if(!restore(expected))throw std::runtime_error("restoration failed");
            }
            return 0;
        }
        throw std::runtime_error("unsupported bass command");
    }catch(const std::exception& e){std::cerr<<Json{{"ok",false},{"error",e.what()}}.dump()<<std::endl;return 1;}
}
