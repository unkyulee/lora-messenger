#pragma once
#include <stdint.h>

// A wake press must be released before it can become a shutdown press.
struct PowerButton {
    static constexpr uint32_t DebounceMs=30, HoldMs=5000;
    bool raw=false, stable=false, enabled=false, armed=false;
    uint32_t changed=0, pressed=0;
    void begin(bool down,uint32_t now) {
        raw=stable=down; enabled=false; armed=false; changed=pressed=now;
    }
    bool update(bool down,uint32_t now) {
        if(down!=raw) { raw=down; changed=now; }
        if(uint32_t(now-changed)<DebounceMs) return false;
        if(!enabled) {
            stable=raw;
            if(!stable) enabled=true;
            return false;
        }
        if(stable!=raw) {
            stable=raw;
            if(stable) pressed=now;
            else if(armed) return true;
        }
        if(stable && uint32_t(now-pressed)>=HoldMs) armed=true;
        return armed && !stable;
    }
};
