#pragma once
#include "chat.h"

namespace network {
constexpr size_t Overhead=12, PacketMax=chat::PacketMax+Overhead;
constexpr uint32_t Lifetime=120000, AckWindow=30000;
constexpr uint8_t Attempts=3;
struct Frame {
    bool ack=false;
    uint8_t attempt=0, hops=1;
    chat::Message message{};
    chat::Ack receipt{};
};
inline size_t encode(const Frame& f,uint8_t* p,size_t cap) {
    if(cap<Overhead || f.attempt>=Attempts || f.hops>1) return 0;
    size_t n=f.ack ? chat::encodeAck(f.receipt,p+8,cap-Overhead) : chat::encode(f.message,p+8,cap-Overhead);
    if(!n) return 0;
    memcpy(p,"LMR2",4); p[4]=f.ack; p[5]=f.attempt; p[6]=f.hops; p[7]=0;
    chat::put32(p+8+n,chat::crc32(p,8+n)); return n+Overhead;
}
inline bool decode(const uint8_t* p,size_t n,Frame& f) {
    if(n<Overhead || n>PacketMax || memcmp(p,"LMR2",4) || p[4]>1 || p[5]>=Attempts || p[6]>1 || p[7] ||
       chat::get32(p+n-4)!=chat::crc32(p,n-4)) return false;
    f.ack=p[4]; f.attempt=p[5]; f.hops=p[6];
    return f.ack ? chat::decodeAck(p+8,n-Overhead,f.receipt) && f.receipt.from!=f.receipt.sender :
        chat::decode(p+8,n-Overhead,f.message);
}
struct Identity {
    uint64_t sender=0, from=0;
    uint32_t session=0, sequence=0;
    uint8_t attempt=0;
    bool ack=false;
    explicit Identity(const Frame& f):sender(f.ack?f.receipt.sender:f.message.sender),
        from(f.ack?f.receipt.from:0),session(f.ack?f.receipt.session:f.message.session),
        sequence(f.ack?f.receipt.sequence:f.message.sequence),attempt(f.attempt),ack(f.ack) {}
    Identity()=default;
    bool operator==(const Identity& b) const {
        return sender==b.sender && from==b.from && session==b.session && sequence==b.sequence && attempt==b.attempt && ack==b.ack;
    }
};
// Fail closed when full: never evict a live duplicate entry to admit traffic.
template<size_t N> struct Seen {
    struct Entry { Identity id; uint32_t expires=0; bool used=false; } entries[N];
    bool insert(const Frame& f,uint32_t now) {
        Entry* free=nullptr; Identity id(f);
        for(auto& e:entries) {
            if(e.used && chat::due(now,e.expires)) e.used=false;
            if(e.used && e.id==id) return false;
            if(!e.used && !free) free=&e;
        }
        if(!free) return false;
        free->id=id; free->expires=now+Lifetime; free->used=true; return true;
    }
};
struct Relay {
    struct Item { Frame frame; uint32_t due=0,expires=0; bool used=false; } queue[8];
    Seen<64> dataSeen, ackSeen;
    bool accept(const Frame& f,uint32_t now,uint32_t random) {
        if(f.hops!=1) return false;
        Item* slot=nullptr;
        // Four reserved ACK slots, four data slots.
        for(size_t i=f.ack?0:4;i<(f.ack?4:8);++i) {
            auto& q=queue[i];
            if(q.used && chat::due(now,q.expires)) q.used=false;
            if(!q.used && !slot) slot=&q;
        }
        if(!slot || !(f.ack?ackSeen:dataSeen).insert(f,now)) return false;
        slot->frame=f; --slot->frame.hops; slot->used=true;
        slot->due=now+150+random%850;
        slot->expires=now+(f.ack?AckWindow:Lifetime); return true;
    }
    Item* next(uint32_t now) {
        for(auto& q:queue) {
            if(q.used && chat::due(now,q.expires)) q.used=false;
            if(q.used && chat::due(now,q.due)) return &q;
        }
        return nullptr;
    }
};
}
