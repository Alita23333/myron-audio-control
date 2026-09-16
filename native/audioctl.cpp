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
using namespace tinyxml2;
static std::string root;
static std::pair<std::string,std::string> generate(const Json& c);
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
static void verifyBase(bool targets) {
    if(prop("ro.product.device")!="myron" || prop("ro.build.fingerprint")!=FINGERPRINT) throw std::runtime_error("unsupported device/firmware; re-audit required");
    if(digest(readFile(root+"/baseline/audio_lowpower_app_list.xml"))!=LOW_STOCK_HASH || digest(readFile(root+"/baseline/audio_effects_config.json"))!=EFFECT_STOCK_HASH) throw std::runtime_error("bundled baseline hash mismatch");
    for(auto id:{"myron_dolby_bili_profile_test","myron_bili_volume_unity","audiolowpowerbypass"}){
        std::string p=std::string("/data/adb/modules/")+id;
        if(exists(p) && !exists(p+"/disable") && !exists(p+"/remove")) throw std::runtime_error(std::string("conflicting enabled module: ")+id);
    }
    if(targets && (digest(readFile(LOW_PATH))!=LOW_STOCK_HASH || digest(readFile(EFFECT_PATH))!=EFFECT_STOCK_HASH)) {
        const std::string installed="/data/adb/modules/myron_audio_control";char resolved[PATH_MAX];
        if(!realpath((installed+"/runtime/current").c_str(),resolved))throw std::runtime_error("target is not original baseline; refusing stacked override");
        std::string old=resolved,prefix=installed+"/runtime/gen-";
        if(old.rfind(prefix,0)!=0||old.substr(prefix.size()).find('/')!=std::string::npos)throw std::runtime_error("unsafe existing generation");
        auto expected=generate(Json::parse(readFile(old+"/config.json")));auto meta=Json::parse(readFile(old+"/meta.json"));
        if(digest(expected.first)!=meta["xmlHash"]||digest(expected.second)!=meta["effectHash"]||readFile(LOW_PATH)!=expected.first||readFile(EFFECT_PATH)!=expected.second)throw std::runtime_error("unknown existing overlay; refusing stacked override");
    }
}
static void validateConfig(const Json& c){
    if(!c.is_object()||c.size()!=4||!c.contains("version")||c["version"]!=1||!c.contains("volumeUnity")||!c["volumeUnity"].is_boolean()||!c.contains("effectMode")||!c["effectMode"].is_string()||!c.contains("appScenes")||!c["appScenes"].is_object()) throw std::runtime_error("invalid config schema");
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
        if(!type||!v||!p||templates.count(type)) throw std::runtime_error("invalid/duplicate template"); templates[type]={v,p};
    }
    for(auto g=r->FirstChildElement("group");g;g=g->NextSiblingElement("group")){
        auto type=g->Attribute("type");if(!type||!templates.count(type)) throw std::runtime_error("group/template mismatch");
        for(auto p=g->FirstChildElement("package");p;p=p->NextSiblingElement("package")){auto n=p->Attribute("name");if(!n||out.count(n))throw std::runtime_error("duplicate/invalid package");out[n]=templates[type];}
    }
    return out;
}
static std::pair<std::string,std::string> generate(const Json& c){
    validateConfig(c);
    std::string stockXml=readFile(root+"/baseline/audio_lowpower_app_list.xml"), xml=stockXml;
    if(c["volumeUnity"].get<bool>()){
        XMLDocument d; d.Parse(stockXml.data(),stockXml.size());
        for(auto t=d.FirstChildElement("packages")->FirstChildElement("config")->FirstChildElement("template");t;t=t->NextSiblingElement("template")) t->SetAttribute("volume","1.0");
        XMLPrinter printer;d.Print(&printer);xml=printer.CStr();
    }
    auto before=rules(stockXml),after=rules(xml); if(before.size()!=after.size()) throw std::runtime_error("package count changed");
    for(auto& [name,rule]:before){auto expected=rule;if(c["volumeUnity"].get<bool>()) expected.first="1.0";if(after.at(name)!=expected) throw std::runtime_error("unexpected XML semantics");}
    std::string stockJson=readFile(root+"/baseline/audio_effects_config.json"), effects=stockJson;
    Json e=Json::parse(stockJson);std::string mode=c["effectMode"];
    if(mode=="default")e["app_profiles"]=Json::object();
    else if(mode=="per-app"){
        for(auto it=c["appScenes"].begin();it!=c["appScenes"].end();++it){
            for(auto& group:e["app_profiles"].items()){
                auto& list=group.value()["packages"];for(auto p=list.begin();p!=list.end();) {if(*p==it.key())p=list.erase(p);else ++p;}
            }
            std::string scene=it.value();if(scene!="default")e["app_profiles"][scene]["packages"].push_back(it.key());
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
    writeFile(p+"/config.json",c.dump(2)+"\n");writeFile(p+"/audio_lowpower_app_list.xml",payload.first);writeFile(p+"/audio_effects_config.json",payload.second);
    Json meta={{"scope",scope},{"xmlHash",digest(payload.first)},{"effectHash",digest(payload.second)}};writeFile(p+"/meta.json",meta.dump(2)+"\n");pointer("pending",name);
}
static void validateGeneration(const std::string& p){
    auto c=Json::parse(readFile(p+"/config.json")),meta=Json::parse(readFile(p+"/meta.json"));auto expected=generate(c);
    if(readFile(p+"/audio_lowpower_app_list.xml")!=expected.first||readFile(p+"/audio_effects_config.json")!=expected.second||meta["xmlHash"]!=digest(expected.first)||meta["effectHash"]!=digest(expected.second)||(meta["scope"]!="test"&&meta["scope"]!="all"))throw std::runtime_error("generation verification failed");
}
static std::string decode64(const std::string& s){
    if(s.size()>90000||s.size()%4)throw std::runtime_error("invalid base64 length");std::string out;unsigned value=0;int bits=-8;bool padding=false;
    for(char c:s){if(c=='='){padding=true;continue;}if(padding)throw std::runtime_error("invalid base64 padding");std::string alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";auto n=alphabet.find(c);if(n==std::string::npos)throw std::runtime_error("invalid base64");value=(value<<6)|int(n);bits+=6;if(bits>=0){out.push_back(char((value>>bits)&255));bits-=8;}}
    if(out.size()>65536)throw std::runtime_error("configuration too large");return out;
}
static std::string logs(){
    FILE* f=popen("/system/bin/logcat -d -s MyronAudioControl:I '*:S'","r");if(!f)return "unavailable";char b[1024];std::string s;while(fgets(b,sizeof b,f)){s+=b;if(s.size()>65536)s.erase(0,s.size()-65536);}pclose(f);return s;
}
int main(int argc,char** argv){
    try{
        if(getuid()!=0)throw std::runtime_error("root backend required");char exe[PATH_MAX];if(!realpath(argv[0],exe))throw std::runtime_error("executable path unavailable");root=exe;root=root.substr(0,root.rfind('/'));root=root.substr(0,root.rfind('/'));
        if(argc<2)throw std::runtime_error("command required");std::string cmd=argv[1];mkdirSafe(root+"/runtime");int lock=open((root+"/runtime/control.lock").c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600);if(lock<0||flock(lock,LOCK_EX))throw std::runtime_error("backend lock failed");
        if(cmd=="check-install"){verifyBase(true);std::cout<<Json({{"ok",true}}).dump()<<std::endl;}
        else if(cmd=="init"){verifyBase(false);if(!exists(root+"/runtime/pending"))stage(Json{{"version",1},{"volumeUnity",true},{"effectMode","stock"},{"appScenes",Json::object()}},"all");std::cout<<"{\"ok\":true}"<<std::endl;}
        else if(cmd=="activate"){verifyBase(true);auto p=generation("pending");validateGeneration(p);pointer("current",p.substr(p.rfind('/')+1));std::cout<<p<<std::endl;}
        else if(cmd=="apply"&&argc==3){verifyBase(false);auto old=generation("pending");auto scope=Json::parse(readFile(old+"/meta.json"))["scope"].get<std::string>();stage(parseInput(decode64(argv[2])),scope);std::cout<<"{\"ok\":true,\"rebootRequired\":true}"<<std::endl;}
        else if(cmd=="scope"&&argc==3){verifyBase(false);stage(Json::parse(readFile(generation("pending")+"/config.json")),argv[2]);std::cout<<"{\"ok\":true,\"rebootRequired\":true}"<<std::endl;}
        else if(cmd=="validate"){verifyBase(false);validateGeneration(generation("pending"));std::cout<<"{\"ok\":true}"<<std::endl;}
        else if(cmd=="status"){
            Json state={{"ok",true},{"disabled",exists(root+"/disable")},{"coverageNotice","Only Zygisk-injected processes and verified service readers; excluded/special output paths not guaranteed"}};
            if(exists(root+"/runtime/pending")){auto p=generation("pending");state["pending"]=Json::parse(readFile(p+"/config.json"));state["pendingMeta"]=Json::parse(readFile(p+"/meta.json"));}
            if(exists(root+"/runtime/current")){auto p=generation("current");state["active"]=Json::parse(readFile(p+"/config.json"));state["activeMeta"]=Json::parse(readFile(p+"/meta.json"));state["rebootRequired"]=p!=generation("pending");}else state["rebootRequired"]=true;
            state["bootReady"]=!state["disabled"].get<bool>()&&!exists(root+"/remove")&&exists(root+"/runtime/boot-ready")&&readFile(root+"/runtime/boot-ready")==readFile("/proc/sys/kernel/random/boot_id");
            state["bootLog"]=exists(root+"/boot-status.log")?readFile(root+"/boot-status.log"):"No boot verification yet";state["nativeReadLog"]=logs();std::cout<<state.dump()<<std::endl;
        }else throw std::runtime_error("unsupported command/arguments");
        close(lock);return 0;
    }catch(const std::exception& e){std::cerr<<Json({{"ok",false},{"error",e.what()}}).dump()<<std::endl;return 1;}
}
