#include "common.hpp"
#include "tinyxml2.h"
#include <sys/system_properties.h>
#include <sys/file.h>
#include <fcntl.h>
#include <limits.h>
#include <regex>
#include <set>
#include <map>
#include <cstdio>
#include <ctime>
#include <cerrno>
#include <iostream>
#include <cmath>
#include <cstring>
#include <sys/xattr.h>
#include <signal.h>
#include <sys/wait.h>
using namespace tinyxml2;
static std::string root,baselineRoot;
static std::pair<std::string,std::string> generate(const Json& c);
static void verifyBase(bool targets);
static Json parseInput(const std::string& s){
    std::vector<std::set<std::string>> keys;
    return Json::parse(s,[&](int depth,Json::parse_event_t event,Json& parsed){
        if(depth>16)throw std::runtime_error("JSON nesting exceeds limit");
        if(event==Json::parse_event_t::object_start)keys.emplace_back();
        else if(event==Json::parse_event_t::key){if(keys.empty()||!keys.back().insert(parsed.get<std::string>()).second)throw std::runtime_error("duplicate JSON key");}
        else if(event==Json::parse_event_t::object_end)keys.pop_back();
        return true;
    });
}
static void mkdirSafe(const std::string& p) { if(mkdir(p.c_str(),0700)!=0 && errno!=EEXIST) throw std::runtime_error("mkdir failed"); }
static void writeFile(const std::string& p,const std::string& s) {
    int fd=open(p.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600);
    if(fd<0) throw std::runtime_error("write failed: "+p);
    size_t n=0; while(n<s.size()){ssize_t r=write(fd,s.data()+n,s.size()-n); if(r<=0){close(fd);throw std::runtime_error("short write");} n+=r;}
    if(fsync(fd)){close(fd);throw std::runtime_error("fsync failed");} close(fd);
}
static std::string prop(const char* name){char b[PROP_VALUE_MAX]{};__system_property_get(name,b);return b;}
static void validateConfig(const Json& c){
    if(!c.is_object()||(c.size()!=4&&c.size()!=5)||(c.size()==5&&!c.contains("bassBoost"))||!c.contains("version")||c["version"]!=1||!c.contains("volumeUnity")||!c["volumeUnity"].is_boolean()||!c.contains("effectMode")||!c["effectMode"].is_string()||!c.contains("appScenes")||!c["appScenes"].is_object()) throw std::runtime_error("invalid config schema");
    if(c.contains("bassBoost")&&(!c["bassBoost"].is_number_integer()||c["bassBoost"]<0||c["bassBoost"]>3))throw std::runtime_error("bassBoost must be an integer from 0 to 3");
    std::string mode=c["effectMode"];
    if(mode!="stock"&&mode!="default"&&mode!="per-app") throw std::runtime_error("invalid effectMode");
    if(c["appScenes"].size()>256) throw std::runtime_error("too many mappings");
    static const std::regex pkg("^[A-Za-z][A-Za-z0-9_]*(\\.[A-Za-z][A-Za-z0-9_]*)+$");
    for(auto it=c["appScenes"].begin();it!=c["appScenes"].end();++it){
        if(it.key().size()>200||!std::regex_match(it.key(),pkg)||!it.value().is_string()) throw std::runtime_error("invalid package/scene");
        std::string scene=it.value(); if(scene!="long_video"&&scene!="short_video"&&scene!="navigation"&&scene!="default") throw std::runtime_error("invalid scene");
    }
}
static std::map<std::string,std::pair<std::string,std::string>> rules(const std::string& xml){
    XMLDocument d; if(d.Parse(xml.data(),xml.size())!=XML_SUCCESS) throw std::runtime_error("invalid XML");
    auto r=d.FirstChildElement("packages"); if(!r||!r->FirstChildElement("config")) throw std::runtime_error("missing XML root/config");
    std::map<std::string,std::pair<std::string,std::string>> templates,out;
    for(auto t=r->FirstChildElement("config")->FirstChildElement("template");t;t=t->NextSiblingElement("template")){
        auto type=t->Attribute("type"),v=t->Attribute("volume"),p=t->Attribute("power_save");
        double volume=0,power=0;
        if(!type||!v||!p||templates.count(type)||t->QueryDoubleAttribute("volume",&volume)!=XML_SUCCESS||t->QueryDoubleAttribute("power_save",&power)!=XML_SUCCESS||!std::isfinite(volume)||!std::isfinite(power)||volume<0||power<0) throw std::runtime_error("invalid/duplicate template"); templates[type]={v,p};
    }
    if(templates.empty())throw std::runtime_error("no volume templates; incompatible XML schema");
    for(auto g=r->FirstChildElement("group");g;g=g->NextSiblingElement("group")){
        auto type=g->Attribute("type");if(!type||!templates.count(type)) throw std::runtime_error("group/template mismatch");
        for(auto p=g->FirstChildElement("package");p;p=p->NextSiblingElement("package")){auto n=p->Attribute("name");if(!n||out.count(n))throw std::runtime_error("duplicate/invalid package");out[n]=templates[type];}
    }
    return out;
}
static void validateStock(const std::string& xml,const std::string& effects){
    rules(xml);auto e=parseInput(effects);
    if(!e.is_object()||!e.contains("app_profiles")||!e["app_profiles"].is_object()||!e.contains("scenario_volume_flag")||!e["scenario_volume_flag"].is_object()||!e.contains("volume_levels")||!e["volume_levels"].is_array())throw std::runtime_error("incompatible effect configuration schema");
    std::set<std::string> packages;
    for(auto& g:e["app_profiles"].items()){
        auto& v=g.value();
        if(!v.is_object()||!v.contains("dolby_profile")||!v["dolby_profile"].is_number_integer()||!v.contains("packages")||!v["packages"].is_array())throw std::runtime_error("invalid scene/profile schema");
        for(auto& p:v["packages"])if(!p.is_string()||!packages.insert(p.get<std::string>()).second)throw std::runtime_error("invalid/duplicate scene package");
    }
}
static std::pair<std::string,std::string> generate(const Json& c){
    validateConfig(c);
    std::string stockXml=readFile(baselineRoot+"/audio_lowpower_app_list.xml"), xml=stockXml;
    std::string stockJson=readFile(baselineRoot+"/audio_effects_config.json");validateStock(stockXml,stockJson);
    if(c["volumeUnity"].get<bool>()){
        XMLDocument d; d.Parse(stockXml.data(),stockXml.size());
        for(auto t=d.FirstChildElement("packages")->FirstChildElement("config")->FirstChildElement("template");t;t=t->NextSiblingElement("template")) t->SetAttribute("volume","1.0");
        XMLPrinter printer;d.Print(&printer);xml=printer.CStr();
    }
    auto before=rules(stockXml),after=rules(xml); if(before.size()!=after.size()) throw std::runtime_error("package count changed");
    for(auto& [name,rule]:before){auto expected=rule;if(c["volumeUnity"].get<bool>()) expected.first="1.0";if(after.at(name)!=expected) throw std::runtime_error("unexpected XML semantics");}
    std::string effects=stockJson;
    Json e=Json::parse(stockJson);std::string mode=c["effectMode"];
    if(mode=="default")e["app_profiles"]=Json::object();
    else if(mode=="per-app"){
        for(auto it=c["appScenes"].begin();it!=c["appScenes"].end();++it){
            for(auto& group:e["app_profiles"].items()){
                auto& list=group.value()["packages"];for(auto p=list.begin();p!=list.end();) {if(*p==it.key())p=list.erase(p);else ++p;}
            }
            std::string scene=it.value();if(scene!="default"){
                if(!e["app_profiles"].contains(scene))throw std::runtime_error("requested scene is unavailable in current firmware: "+scene);
                e["app_profiles"][scene]["packages"].push_back(it.key());
            }
        }
    }
    if(mode!="stock")effects=e.dump(4)+"\n";
    Json check=e,base=Json::parse(stockJson);check.erase("app_profiles");base.erase("app_profiles");if(check!=base)throw std::runtime_error("non-profile effect settings changed");
    std::set<std::string> seen;for(auto& group:e["app_profiles"].items())for(auto& p:group.value()["packages"])if(!seen.insert(p.get<std::string>()).second)throw std::runtime_error("duplicate scene package");
    return {xml,effects};
}
static std::string generation(const std::string& link){
    char p[PATH_MAX];if(!realpath((root+"/runtime/"+link).c_str(),p))throw std::runtime_error("no "+link+" generation");
    std::string path=p,prefix=root+"/runtime/gen-";if(path.rfind(prefix,0)!=0||path.substr(prefix.size()).find('/')!=std::string::npos)throw std::runtime_error("unsafe generation link");return path;
}
static void pointer(const std::string& link,const std::string& name){
    std::string tmp=root+"/runtime/."+link+"-"+std::to_string(getpid());unlink(tmp.c_str());if(symlink(name.c_str(),tmp.c_str())||rename(tmp.c_str(),(root+"/runtime/"+link).c_str()))throw std::runtime_error("pointer commit failed");
    int fd=open((root+"/runtime").c_str(),O_RDONLY|O_DIRECTORY);if(fd>=0){fsync(fd);close(fd);}
}
static void stage(const Json& c,const std::string& scope){
    if(scope!="test"&&scope!="all")throw std::runtime_error("invalid rollout scope");
    auto payload=generate(c);timespec ts{};clock_gettime(CLOCK_REALTIME,&ts);
    std::string name="gen-"+std::to_string(ts.tv_sec)+"-"+std::to_string(ts.tv_nsec)+"-"+std::to_string(getpid()),p=root+"/runtime/"+name;mkdirSafe(p);
    Json config=c;if(!config.contains("bassBoost"))config["bassBoost"]=0;
    writeFile(p+"/config.json",config.dump(2)+"\n");writeFile(p+"/audio_lowpower_app_list.xml",payload.first);writeFile(p+"/audio_effects_config.json",payload.second);
    Json meta={{"scope",scope},{"xmlHash",digest(payload.first)},{"effectHash",digest(payload.second)},{"baseXmlHash",digest(readFile(baselineRoot+"/audio_lowpower_app_list.xml"))},{"baseEffectHash",digest(readFile(baselineRoot+"/audio_effects_config.json"))}};writeFile(p+"/meta.json",meta.dump(2)+"\n");pointer("pending",name);
}
static void validateGeneration(const std::string& p){
    auto c=Json::parse(readFile(p+"/config.json")),meta=Json::parse(readFile(p+"/meta.json"));auto expected=generate(c);
    if(readFile(p+"/audio_lowpower_app_list.xml")!=expected.first||readFile(p+"/audio_effects_config.json")!=expected.second||meta["xmlHash"]!=digest(expected.first)||meta["effectHash"]!=digest(expected.second)||(meta["scope"]!="test"&&meta["scope"]!="all"))throw std::runtime_error("generation verification failed");
}
static std::string resolveBaseline(){
    if(!exists(root+"/runtime/base-current"))return root+"/baseline";
    char p[PATH_MAX];if(!realpath((root+"/runtime/base-current").c_str(),p))throw std::runtime_error("baseline snapshot unavailable");
    std::string path=p,prefix=root+"/runtime/bases/base-";
    if(path.rfind(prefix,0)!=0||path.substr(prefix.size()).find('/')!=std::string::npos)throw std::runtime_error("unsafe baseline link");
    auto meta=Json::parse(readFile(path+"/meta.json"));
    if(meta["xmlHash"]!=digest(readFile(path+"/audio_lowpower_app_list.xml"))||meta["effectHash"]!=digest(readFile(path+"/audio_effects_config.json")))throw std::runtime_error("baseline snapshot integrity failed");
    return path;
}
static bool originalOdmMount(const std::string& path){
    std::istringstream lines(readFile("/proc/self/mountinfo",4194304));std::string line,best,options,filesystem;
    while(std::getline(lines,line)){
        auto split=line.find(" - ");if(split==std::string::npos)continue;
        std::istringstream left(line.substr(0,split)),right(line.substr(split+3));
        std::string id,parent,dev,mountRoot,mountPoint,flags,fs;
        left>>id>>parent>>dev>>mountRoot>>mountPoint>>flags;right>>fs;
        if((path==mountPoint||path.rfind(mountPoint+"/",0)==0)&&mountPoint.size()>best.size()){best=mountPoint;options=flags;filesystem=fs;}
    }
    return best=="/odm"&&(filesystem=="erofs"||filesystem=="ext4"||filesystem=="squashfs")&&(","+options+",").find(",ro,")!=std::string::npos;
}
static Json fileMetadata(const char* path){
    struct stat st{};if(stat(path,&st))throw std::runtime_error("baseline stat failed");
    char label[256]{};auto n=getxattr(path,"security.selinux",label,sizeof(label)-1);
    return {{"uid",st.st_uid},{"gid",st.st_gid},{"mode",st.st_mode&07777},{"selinux",n>0?std::string(label,strnlen(label,size_t(n))):"unavailable"}};
}
static void verifyBase(bool targets){
    if(prop("ro.product.device")!="myron")throw std::runtime_error("unsupported device; myron required");
    for(auto id:{"myron_dolby_bili_profile_test","myron_bili_volume_unity","audiolowpowerbypass"}){
        std::string p=std::string("/data/adb/modules/")+id;
        if(exists(p)&&!exists(p+"/disable")&&!exists(p+"/remove"))throw std::runtime_error(std::string("conflicting enabled module: ")+id);
    }
    baselineRoot=resolveBaseline();
    validateStock(readFile(baselineRoot+"/audio_lowpower_app_list.xml"),readFile(baselineRoot+"/audio_effects_config.json"));
    if(!targets)return;
    const auto xml=readFile(LOW_PATH),effects=readFile(EFFECT_PATH);
    if(!originalOdmMount(LOW_PATH)||!originalOdmMount(EFFECT_PATH)){
        // Upgrade while our old generation is mounted: use its preserved baseline,
        // never adopt the visible overlay as stock. Unknown mounts are rejected.
        auto p=generation("current");validateGeneration(p);
        if(xml!=readFile(p+"/audio_lowpower_app_list.xml")||effects!=readFile(p+"/audio_effects_config.json"))throw std::runtime_error("unknown existing overlay; cannot capture original audio files");
        return;
    }
    validateStock(xml,effects);
    mkdirSafe(root+"/runtime/bases");
    auto name="bases/base-"+digest(xml)+"-"+digest(effects),p=root+"/runtime/"+name;
    if(!exists(p)){
        mkdirSafe(p);
        writeFile(p+"/audio_lowpower_app_list.xml",xml);writeFile(p+"/audio_effects_config.json",effects);
        Json meta={{"xmlHash",digest(xml)},{"effectHash",digest(effects)},{"fingerprint",prop("ro.build.fingerprint")},{"source","read-only ODM before module mount"},{"xmlMetadata",fileMetadata(LOW_PATH)},{"effectMetadata",fileMetadata(EFFECT_PATH)}};
        writeFile(p+"/meta.json",meta.dump(2)+"\n");
        for(auto file:{"audio_lowpower_app_list.xml","audio_effects_config.json","meta.json"})if(chmod((p+"/"+file).c_str(),0444))throw std::runtime_error("cannot protect baseline snapshot");
    }
    if(readFile(p+"/audio_lowpower_app_list.xml")!=xml||readFile(p+"/audio_effects_config.json")!=effects)throw std::runtime_error("existing baseline snapshot mismatch");
    pointer("base-current",name);baselineRoot=resolveBaseline();
}
static std::string decode64(const std::string& s){
    if(s.size()>90000||s.size()%4)throw std::runtime_error("invalid base64 length");std::string out;unsigned value=0;int bits=-8;bool padding=false;
    for(char c:s){if(c=='='){padding=true;continue;}if(padding)throw std::runtime_error("invalid base64 padding");std::string alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";auto n=alphabet.find(c);if(n==std::string::npos)throw std::runtime_error("invalid base64");value=(value<<6)|int(n);bits+=6;if(bits>=0){out.push_back(char((value>>bits)&255));bits-=8;}}
    if(out.size()>65536)throw std::runtime_error("configuration too large");return out;
}
static std::string logs(){
    FILE* f=popen("/system/bin/logcat -d -s MyronAudioControl:I '*:S'","r");if(!f)return "unavailable";char b[1024];std::string s;while(fgets(b,sizeof b,f)){s+=b;if(s.size()>65536)s.erase(0,s.size()-65536);}pclose(f);return s;
}
static Json installedApps(){
    int fds[2];if(pipe(fds))throw std::runtime_error("catalog pipe failed");
    auto child=fork();if(child<0){close(fds[0]);close(fds[1]);throw std::runtime_error("catalog process failed");}
    if(child==0){
        close(fds[0]);dup2(fds[1],STDOUT_FILENO);close(fds[1]);
        setenv("CLASSPATH",(root+"/lib/app-catalog.jar").c_str(),1);
        execl("/system/bin/timeout","timeout","20","/system/bin/app_process","/system/bin","dev.myron.audio.AppCatalog",nullptr);_exit(127);
    }
    close(fds[1]);std::string output;char buffer[4096];ssize_t n;bool overflow=false;
    while((n=read(fds[0],buffer,sizeof buffer))>0){output.append(buffer,n);if(output.size()>4*1024*1024){overflow=true;kill(child,SIGKILL);break;}}
    close(fds[0]);int status=0;while(waitpid(child,&status,0)<0&&errno==EINTR){}
    if(overflow||n<0||!WIFEXITED(status)||WEXITSTATUS(status))throw std::runtime_error("application list unavailable; retry after unlock");
    auto result=Json::parse(output);if(!result.value("ok",false)||!result.contains("apps")||!result["apps"].is_array())throw std::runtime_error("invalid application catalog");return result;
}
int main(int argc,char** argv){
    try{
        if(getuid()!=0)throw std::runtime_error("root backend required");char exe[PATH_MAX];if(!realpath(argv[0],exe))throw std::runtime_error("executable path unavailable");root=exe;root=root.substr(0,root.rfind('/'));root=root.substr(0,root.rfind('/'));
        if(argc<2)throw std::runtime_error("command required");std::string cmd=argv[1];mkdirSafe(root+"/runtime");int lock=open((root+"/runtime/control.lock").c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600);if(lock<0||flock(lock,LOCK_EX))throw std::runtime_error("backend lock failed");
        if(cmd=="apps"&&argc==2){std::cout<<installedApps().dump()<<std::endl;}
        else if(cmd=="check-install"){verifyBase(true);std::cout<<Json({{"ok",true}}).dump()<<std::endl;}
        else if(cmd=="init"){verifyBase(false);if(!exists(root+"/runtime/pending"))stage(Json{{"version",1},{"volumeUnity",true},{"effectMode","stock"},{"appScenes",Json::object()}},"all");std::cout<<"{\"ok\":true}"<<std::endl;}
        else if(cmd=="activate"){verifyBase(true);auto previous=generation("pending");auto c=Json::parse(readFile(previous+"/config.json")),meta=Json::parse(readFile(previous+"/meta.json"));stage(c,meta["scope"]);auto p=generation("pending");validateGeneration(p);pointer("current",p.substr(p.rfind('/')+1));std::cout<<p<<std::endl;}
        else if(cmd=="apply"&&argc==3){verifyBase(false);auto old=generation("pending");auto scope=Json::parse(readFile(old+"/meta.json"))["scope"].get<std::string>();stage(parseInput(decode64(argv[2])),scope);std::cout<<"{\"ok\":true,\"rebootRequired\":true}"<<std::endl;}
        else if(cmd=="scope"&&argc==3){verifyBase(false);stage(Json::parse(readFile(generation("pending")+"/config.json")),argv[2]);std::cout<<"{\"ok\":true,\"rebootRequired\":true}"<<std::endl;}
        else if(cmd=="validate"){verifyBase(false);validateGeneration(generation("pending"));std::cout<<"{\"ok\":true}"<<std::endl;}
        else if(cmd=="status"){
            Json state={{"ok",true},{"disabled",exists(root+"/disable")},{"coverageNotice","Only Zygisk-injected processes and verified service readers; excluded/special output paths not guaranteed"}};
            if(exists(root+"/runtime/pending")){auto p=generation("pending");state["pending"]=Json::parse(readFile(p+"/config.json"));state["pendingMeta"]=Json::parse(readFile(p+"/meta.json"));}
            if(exists(root+"/runtime/current")){auto p=generation("current");state["active"]=Json::parse(readFile(p+"/config.json"));state["activeMeta"]=Json::parse(readFile(p+"/meta.json"));state["rebootRequired"]=p!=generation("pending");}else state["rebootRequired"]=true;
            state["bootReady"]=!state["disabled"].get<bool>()&&!exists(root+"/remove")&&exists(root+"/runtime/boot-ready")&&readFile(root+"/runtime/boot-ready")==readFile("/proc/sys/kernel/random/boot_id");
            state["bootLog"]=exists(root+"/boot-status.log")?readFile(root+"/boot-status.log"):"No boot verification yet";state["nativeReadLog"]=logs();
            state["unmountOnlyPolicyRecognized"]=unmountOnlyPolicy();state["versionPolicy"]="capability/schema based; no firmware or Zygisk version whitelist";
            state["baselineSource"]=resolveBaseline();if(exists(state["baselineSource"].get<std::string>()+"/meta.json"))state["baselineMeta"]=Json::parse(readFile(state["baselineSource"].get<std::string>()+"/meta.json"));
            state["bass"]={{"state","not_started"},{"running",false}};
            if(exists(root+"/runtime/bass-status.json")){
                auto b=Json::parse(readFile(root+"/runtime/bass-status.json"));bool current=b.value("bootId",std::string())==trim(readFile("/proc/sys/kernel/random/boot_id"));
                int pid=b.value("pid",0);char exe[PATH_MAX]{};bool running=false;
                if(current&&pid>1&&kill(pid,0)==0){auto n=readlink(("/proc/"+std::to_string(pid)+"/exe").c_str(),exe,sizeof(exe)-1);running=n>0&&std::string(exe)==root+"/bin/bassctl";}
                b["currentBoot"]=current;b["running"]=running;
                if(!current)b["state"]="stale";
                else if(!running&&b.value("owned",false))b["state"]="service_stopped_check_restore";
                state["bass"]=b;
            }
            std::cout<<state.dump()<<std::endl;
        }else throw std::runtime_error("unsupported command/arguments");
        close(lock);return 0;
    }catch(const std::exception& e){std::cerr<<Json({{"ok",false},{"error",e.what()}}).dump()<<std::endl;return 1;}
}
