#pragma once
#include <string>
#include <stdexcept>
namespace bass {
constexpr unsigned mask=0x03e0, factory=5;
inline unsigned field(unsigned word){return (word>>5)&31;}
inline unsigned replace(unsigned word,unsigned code){return (word&~mask)|(code<<5);}
inline unsigned target(int level){if(level<0||level>3)throw std::runtime_error("invalid bass level");return factory-unsigned(level);}
struct Snapshot { bool identity=false,normal=false,active=false,stereo=false,gainMatch=false; unsigned word=0; };
struct Engine {
    bool owned=false,armed=true;
    unsigned expected=factory;
    std::string state="waiting";
    // Return a register word to write, or -1. Once the system takes ownership,
    // do not reapply until a genuine inactive/mode/route transition is observed.
    int tick(const Snapshot& s,int level){
        auto desired=target(level);
        if(!s.identity){state="unsupported_hardware";return -1;}
        if(!level||!s.normal||!s.active||!s.stereo||!s.gainMatch){
            bool release=owned&&field(s.word)==expected;
            owned=false;armed=true;
            state=!level?"stock":!s.active?"idle":!s.normal?"communication_or_mode_unknown":!s.stereo?"other_route":"gain_changed";
            return release?int(replace(s.word,factory)):-1;
        }
        if(owned){
            if(field(s.word)==expected){state="active";return -1;}
            owned=false;armed=false;state="system_owned";return -1;
        }
        if(!armed){state="system_owned";return -1;}
        if(field(s.word)!=factory){state="nonfactory_attenuation";return -1;}
        owned=true;expected=desired;state="active";
        return int(replace(s.word,desired));
    }
};
}
