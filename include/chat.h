#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace chat {
constexpr size_t NameMax = 16, TextMax = 160, HistoryMax = 24;
constexpr size_t PacketMax = 24 + NameMax + TextMax + 4;
constexpr size_t SnapshotMax = 10 + NameMax + HistoryMax * (2 + PacketMax) + 4;
constexpr size_t AckSize = 32;

inline uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t crc = ~uint32_t(0);
    while (n--) {
        crc ^= *p++;
        for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}
inline void put32(uint8_t* p, uint32_t v) { for (int i=0;i<4;++i) p[i]=uint8_t(v>>(8*i)); }
inline uint32_t get32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24;
}
inline size_t boundedLength(const char* s, size_t max) {
    size_t n=0; while(n<=max && s[n]) ++n; return n;
}
inline bool validText(const char* s, size_t n) {
    bool visible=false;
    for(size_t i=0;i<n;++i) {
        if(uint8_t(s[i])<32 || uint8_t(s[i])>126) return false;
        visible |= s[i]!=' ';
    }
    return visible;
}
inline void trim(char* s) {
    size_t n=strlen(s), start=0;
    while(start<n && s[start]==' ') ++start;
    while(n>start && s[n-1]==' ') --n;
    memmove(s,s+start,n-start); s[n-start]=0;
}
struct Message {
    uint64_t sender=0;
    uint32_t session=0, sequence=0;
    char name[NameMax+1]={};
    char text[TextMax+1]={};
};
inline bool sameId(const Message& a,const Message& b) {
    return a.sender==b.sender && a.session==b.session && a.sequence==b.sequence;
}
inline size_t encode(const Message& m,uint8_t* out,size_t capacity) {
    size_t nl=boundedLength(m.name,NameMax), tl=boundedLength(m.text,TextMax);
    size_t n=28+nl+tl;
    if(!m.sender || !m.session || !m.sequence || nl>NameMax || tl>TextMax ||
       !validText(m.name,nl) || !validText(m.text,tl) || n>capacity) return 0;
    memcpy(out,"LMC1",4); out[4]=1; out[5]=uint8_t(nl); out[6]=uint8_t(tl); out[7]=0;
    put32(out+8,uint32_t(m.sender)); put32(out+12,uint32_t(m.sender>>32));
    put32(out+16,m.session); put32(out+20,m.sequence);
    memcpy(out+24,m.name,nl); memcpy(out+24+nl,m.text,tl);
    put32(out+n-4,crc32(out,n-4)); return n;
}
inline bool decode(const uint8_t* p,size_t n,Message& out) {
    if(n<30 || n>PacketMax || memcmp(p,"LMC1",4) || p[4]!=1 || p[7]!=0) return false;
    size_t nl=p[5],tl=p[6];
    if(nl>NameMax || tl>TextMax || n!=28+nl+tl || get32(p+n-4)!=crc32(p,n-4)) return false;
    if(!validText(reinterpret_cast<const char*>(p+24),nl) ||
       !validText(reinterpret_cast<const char*>(p+24+nl),tl)) return false;
    Message m;
    m.sender=uint64_t(get32(p+8)) | uint64_t(get32(p+12))<<32;
    m.session=get32(p+16); m.sequence=get32(p+20);
    if(!m.sender || !m.session || !m.sequence) return false;
    memcpy(m.name,p+24,nl); memcpy(m.text,p+24+nl,tl); out=m; return true;
}
// Acknowledgement of one message identity, sent by the receiving device `from`.
struct Ack {
    uint64_t sender=0;
    uint32_t session=0, sequence=0;
    uint64_t from=0;
};
inline bool acknowledges(const Ack& a,const Message& m) {
    return a.sender==m.sender && a.session==m.session && a.sequence==m.sequence;
}
inline size_t encodeAck(const Ack& a,uint8_t* out,size_t capacity) {
    if(capacity<AckSize || !a.sender || !a.session || !a.sequence || !a.from) return 0;
    memcpy(out,"LMA1",4);
    put32(out+4,uint32_t(a.sender)); put32(out+8,uint32_t(a.sender>>32));
    put32(out+12,a.session); put32(out+16,a.sequence);
    put32(out+20,uint32_t(a.from)); put32(out+24,uint32_t(a.from>>32));
    put32(out+28,crc32(out,28)); return AckSize;
}
inline bool decodeAck(const uint8_t* p,size_t n,Ack& out) {
    if(n!=AckSize || memcmp(p,"LMA1",4) || get32(p+28)!=crc32(p,28)) return false;
    Ack a;
    a.sender=uint64_t(get32(p+4)) | uint64_t(get32(p+8))<<32;
    a.session=get32(p+12); a.sequence=get32(p+16);
    a.from=uint64_t(get32(p+20)) | uint64_t(get32(p+24))<<32;
    if(!a.sender || !a.session || !a.sequence || !a.from) return false;
    out=a; return true;
}
struct State {
    uint32_t generation=0;
    char name[NameMax+1]={};
    Message messages[HistoryMax];
    size_t count=0;
    bool add(const Message& m) {
        for(size_t i=0;i<count;++i) if(sameId(messages[i],m)) return false;
        if(count==HistoryMax) {
            memmove(messages,messages+1,(HistoryMax-1)*sizeof(Message)); --count;
        }
        messages[count++]=m; return true;
    }
};
inline size_t save(const State& s,uint8_t* p,size_t cap) {
    size_t nl=boundedLength(s.name,NameMax);
    if(nl>NameMax || (nl && !validText(s.name,nl)) || s.count>HistoryMax || cap<14+nl) return 0;
    memcpy(p,"LMS1",4); put32(p+4,s.generation); p[8]=uint8_t(nl); p[9]=uint8_t(s.count);
    memcpy(p+10,s.name,nl); size_t pos=10+nl;
    for(size_t i=0;i<s.count;++i) {
        if(pos+6>cap) return 0;
        size_t n=encode(s.messages[i],p+pos+2,cap-pos-6);
        if(!n) return 0;
        p[pos]=uint8_t(n); p[pos+1]=uint8_t(n>>8); pos+=2+n;
    }
    put32(p+pos,crc32(p,pos)); return pos+4;
}
inline bool restore(const uint8_t* p,size_t n,State& out) {
    if(n<14 || n>SnapshotMax || memcmp(p,"LMS1",4) || get32(p+n-4)!=crc32(p,n-4)) return false;
    size_t nl=p[8],count=p[9],pos=10+nl;
    if(nl>NameMax || count>HistoryMax || pos+4>n || (nl && !validText(reinterpret_cast<const char*>(p+10),nl))) return false;
    // Validate first, then decode into the destination. Avoid a 5 KB State
    // temporary on the Wio's 4 KB Arduino task stack.
    struct Id { uint64_t sender; uint32_t session, sequence; } ids[HistoryMax];
    for(size_t i=0;i<count;++i) {
        if(pos+2>n-4) return false;
        size_t len=size_t(p[pos]) | size_t(p[pos+1])<<8; pos+=2;
        if(len>n-4-pos) return false;
        Message m; if(!decode(p+pos,len,m)) return false;
        for(size_t j=0;j<i;++j)
            if(ids[j].sender==m.sender && ids[j].session==m.session && ids[j].sequence==m.sequence) return false;
        ids[i]={m.sender,m.session,m.sequence};
        pos+=len;
    }
    if(pos!=n-4) return false;
    out.generation=get32(p+4); memset(out.name,0,sizeof(out.name)); memcpy(out.name,p+10,nl);
    out.count=count; pos=10+nl;
    for(size_t i=0;i<count;++i) {
        size_t len=size_t(p[pos]) | size_t(p[pos+1])<<8; pos+=2;
        decode(p+pos,len,out.messages[i]); pos+=len;
    }
    return true;
}
inline bool newer(uint32_t a,uint32_t b) { return int32_t(a-b)>0; }
inline bool due(uint32_t now,uint32_t deadline) { return int32_t(now-deadline)>=0; }
// A conservative 5% airtime budget. Credit accrues at 1 ms of airtime per 20 ms,
// starts empty at boot (rebooting cannot bypass it) and is capped at CapMs so a
// burst stays short. Every attempted transmission is charged, including failures.
struct AirtimeGate {
    static constexpr uint32_t CapMs=4000;
    uint32_t last=0, credit=0; // credit is stored as elapsed ms, i.e. airtime x 20
    void start(uint32_t now) { last=now; credit=0; }
    void update(uint32_t now) {
        const uint32_t elapsed=now-last, cap=CapMs*20;
        last=now; credit=elapsed>=cap-credit ? cap : credit+elapsed;
    }
    bool ready(uint32_t now,uint32_t airtimeMs) { update(now); return credit>=airtimeMs*20; }
    void charge(uint32_t now,uint32_t airtimeMs) {
        update(now); credit=credit>airtimeMs*20 ? credit-airtimeMs*20 : 0;
    }
};
}
