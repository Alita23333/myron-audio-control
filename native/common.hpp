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
inline const char* LOW_STOCK_HASH="352c981839b5619b5b61b28c6ca1bf8ca59fe8972c67510757f218b5cc7b706d";
inline const char* EFFECT_STOCK_HASH="cd873f82c5d5f8f339bb2455c63c7c37e64fbb24959b912f4f5c28ad06c8b25b";
inline const char* FINGERPRINT="Redmi/myron/myron:17/CP2A.260605.016/OS4.0.0.25.XPMCNXM:user/release-keys";
