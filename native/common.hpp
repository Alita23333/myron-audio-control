#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#include "json.hpp"
#include "picosha2.h"
using Json = nlohmann::json;
inline std::string readFile(const std::string& p, size_t max=262144) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("read failed: "+p);
    std::string s((std::istreambuf_iterator<char>(f)), {});
    if (s.size()>max) throw std::runtime_error("file exceeds size limit");
    return s;
}
inline std::string digest(const std::string& s) { return picosha2::hash256_hex_string(s); }
inline bool exists(const std::string& p) { struct stat s{}; return stat(p.c_str(),&s)==0; }
inline const char* LOW_PATH="/odm/etc/audio/audio_lowpower_app_list.xml";
inline const char* EFFECT_PATH="/odm/etc/audio/audio_effects_config.json";
inline std::string trim(std::string s) {
    auto first=s.find_first_not_of(" \t\r\n\0",0,5);
    if(first==std::string::npos)return {};
    auto last=s.find_last_not_of(" \t\r\n\0",std::string::npos,5);
    return s.substr(first,last-first+1);
}
inline bool unmountOnlyPolicy() {
    // Capability/policy check, deliberately independent of provider version.
    // An enforced or unrecognized policy never authorizes a denylisted hook.
    try {
        const std::string next="/data/adb/modules/zygisksu";
        if(!exists(next)||exists(next+"/disable")||exists(next+"/remove"))return false;
        return trim(readFile("/data/adb/zygisksu/denylist_enforce",32))=="2";
    }catch(...){return false;}
}
