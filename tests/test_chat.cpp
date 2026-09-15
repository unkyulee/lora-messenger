#include <assert.h>
#include <stdio.h>
#include <vector>
#include <random>
#include "chat.h"
#include "device.h"
#include "Arduino.h"
uint32_t fakeTime=100;
FakeSerial Serial;
std::vector<uint8_t> files[2], received, sent;
bool failWrite=false, channelBusy=false, startFails=false, screenPower=true;
int txResult=0, chimes=0;
Screen lastScreen;
Input nextInput;
bool deviceBegin() { return true; }
void deviceTick() {}
// Input polling takes time on real hardware (the Pager reads its keyboard over I2C).
Input deviceInput() { fakeTime+=2; Input i=nextInput; nextInput={}; return i; }
void deviceDraw(const Screen& s) { lastScreen=s; }
void deviceDisplay(bool on) { screenPower=on; }
int deviceColumns() { return 21; }
int deviceBattery() { return 80; }
void deviceChime() { ++chimes; }
const char* deviceModifier() { return ""; }
uint64_t deviceId() { return 1234; }
uint32_t deviceRandom() { return 76543; }
bool storageBegin() { return true; }
size_t storageRead(int slot,uint8_t* p,size_t cap) {
    if(files[slot].size()>cap) return 0;
    memcpy(p,files[slot].data(),files[slot].size()); return files[slot].size();
}
bool storageWrite(int slot,const uint8_t* p,size_t n) {
    files[slot].assign(p,p+(failWrite ? n/2 : n)); return !failWrite;
}
bool radioBegin() { return true; }
int radioReceive(uint8_t* p,size_t cap) {
    if(received.size()>cap) return -1;
    int n=int(received.size()); memcpy(p,received.data(),n); received.clear(); return n;
}
bool radioBusy() { return channelBusy; }
bool radioSend(const uint8_t* p,size_t n) { sent.assign(p,p+n); return !startFails; }
int radioResult() { int r=txResult; txResult=0; return r; }
uint32_t radioAirtime(size_t n) { return uint32_t(n)*5; }
#include "../src/main.cpp"

chat::Message message(uint32_t seq=1) {
    chat::Message m; m.sender=999; m.session=42; m.sequence=seq;
    strcpy(m.name,"Alice"); strcpy(m.text,"Hello everyone!"); return m;
}
void key(Key k,char c=0) {
    if(!displayOn) { nextInput={Key::Up,0}; loop(); } // the waking press is consumed
    nextInput={k,c}; loop();
}
void deliver(const chat::Message& m) {
    uint8_t b[chat::PacketMax]; size_t n=chat::encode(m,b,sizeof(b));
    assert(n); received.assign(b,b+n); loop();
}
void deliverAck(const chat::Message& m,uint64_t from=555) {
    chat::Ack a; a.sender=m.sender; a.session=m.session; a.sequence=m.sequence; a.from=from;
    uint8_t b[chat::AckSize]; size_t n=chat::encodeAck(a,b,sizeof(b));
    assert(n); received.assign(b,b+n); loop();
}
// Let the scheduled acknowledgement go out, complete its transmission, and check it.
bool flushAck(const chat::Message& expected) {
    sent.clear(); fakeTime+=1100; loop();
    chat::Ack a;
    bool ok=!sent.empty() && chat::decodeAck(sent.data(),sent.size(),a) &&
        chat::acknowledges(a,expected) && a.from==deviceId();
    if(!sent.empty()) { txResult=1; loop(); }
    return ok;
}
void protocolTests() {
    auto m=message(); uint8_t b[chat::SnapshotMax]={};
    size_t n=chat::encode(m,b,sizeof(b)); assert(n==48);
    chat::Message decoded; assert(chat::decode(b,n,decoded)); assert(chat::sameId(m,decoded));
    assert(!strcmp(m.text,decoded.text));
    for(size_t i=0;i<n;++i) { b[i]^=1; assert(!chat::decode(b,n,decoded)); b[i]^=1; }
    for(size_t len=0;len<n;++len) assert(!chat::decode(b,len,decoded));
    assert(!chat::decode(b,n+1,decoded));
    memset(m.name,'n',chat::NameMax); m.name[chat::NameMax]=0;
    memset(m.text,'x',chat::TextMax); m.text[chat::TextMax]=0;
    assert(chat::encode(m,b,sizeof(b))==chat::PacketMax);
    assert(chat::decode(b,chat::PacketMax,decoded));
    memset(m.text,' ',chat::TextMax); assert(!chat::encode(m,b,sizeof(b)));
    m=message(); m.text[3]='\n'; assert(!chat::encode(m,b,sizeof(b)));
    chat::State s; strcpy(s.name,"Bob");
    for(unsigned i=1;i<=40;++i) assert(s.add(message(i)));
    assert(s.count==24 && s.messages[0].sequence==17);
    assert(!s.add(message(40)));
    n=chat::save(s,b,sizeof(b)); assert(n);
    chat::State loaded; assert(chat::restore(b,n,loaded)); assert(loaded.count==24);
    for(size_t len=0;len<n;++len) assert(!chat::restore(b,len,loaded));
    for(size_t i=0;i<n;i+=17) { b[i]^=1; assert(!chat::restore(b,n,loaded)); b[i]^=1; }
    // Hostile packets with a valid checksum must still fail structural validation.
    n=chat::encode(message(),b,sizeof(b)); b[5]=255; chat::put32(b+n-4,chat::crc32(b,n-4));
    assert(!chat::decode(b,n,decoded));
    std::mt19937 rng(123);
    for(int i=0;i<10000;++i) {
        size_t len=rng()%sizeof(b);
        for(size_t j=0;j<len;++j) b[j]=uint8_t(rng());
        chat::restore(b,len,loaded); if(len<=chat::PacketMax) chat::decode(b,len,decoded);
    }
    // Acknowledgements: exact size, CRC, identity match, and never mistaken for a message.
    chat::Ack a; a.sender=999; a.session=42; a.sequence=7; a.from=555;
    uint8_t ab[chat::AckSize]; assert(chat::encodeAck(a,ab,sizeof(ab))==chat::AckSize);
    chat::Ack da; assert(chat::decodeAck(ab,sizeof(ab),da) && da.from==555);
    assert(chat::acknowledges(da,message(7)) && !chat::acknowledges(da,message(8)));
    for(size_t i=0;i<sizeof(ab);++i) { ab[i]^=1; assert(!chat::decodeAck(ab,sizeof(ab),da)); ab[i]^=1; }
    assert(!chat::decodeAck(ab,sizeof(ab)-1,da)); assert(!chat::decode(ab,sizeof(ab),decoded));
    a.from=0; assert(!chat::encodeAck(a,ab,sizeof(ab)));
    // Airtime budget: empty at start, accrues 1/20 of elapsed time across rollover, capped.
    chat::AirtimeGate g; g.start(0xfffffff0u);
    assert(!g.ready(0xfffffff1u,10));
    assert(g.ready(0xfffffff0u+201,10));
    g.charge(0xfffffff0u+201,10); assert(!g.ready(0xfffffff0u+202,10));
    assert(g.ready(1000000,chat::AirtimeGate::CapMs) && !g.ready(1000000,chat::AirtimeGate::CapMs+1));
    assert(chat::newer(1,0xffffffffu));
    puts("PASS: packet and acknowledgement validation, corruption, bounds, history, airtime budget, clock rollover");
}
void appTests() {
    setup(); assert(view==View::Name);
    assert(strstr(lastScreen.lines[0],"Your name?") && strstr(lastScreen.lines[0],"80%"));
    key(Key::Send); assert(view==View::Name); // empty names rejected
    key(Key::Character,'B'); key(Key::Character,'o'); key(Key::Character,'b');
    failWrite=true; key(Key::Send); assert(view==View::Name && !state.name[0]);
    failWrite=false; key(Key::Send); assert(view==View::Inbox && !strcmp(state.name,"Bob"));
    key(Key::Character,'H'); key(Key::Character,'i'); key(Key::Send);
    assert(queued && !sending && sent.empty()); // airtime credit starts empty after boot
    key(Key::Back); assert(!queued && !strcmp(draft,"Hi")); // cancel retains draft
    key(Key::Send); fakeTime+=25000; loop(); assert(sending);
    txResult=-1; loop(); assert(view==View::Compose && !strcmp(draft,"Hi"));
    assert(state.count==0); // failed send cannot enter sent history
    // Transmitted, but nobody acknowledged: back to the editor with the text.
    key(Key::Send); fakeTime+=25000; loop(); assert(sending);
    chat::Message first; assert(chat::decode(sent.data(),sent.size(),first));
    txResult=1; loop(); assert(awaiting && !sending);
    fakeTime+=AckWindowMs+1; loop();
    assert(!awaiting && view==View::Compose && !strcmp(draft,"Hi") && state.count==0);
    assert(strstr(lastScreen.lines[0],"Not delivered"));
    // Resending unchanged text keeps its identity; another device's acknowledgement completes it.
    key(Key::Send); fakeTime+=25000; loop(); assert(sending);
    chat::Message outgoing; assert(chat::decode(sent.data(),sent.size(),outgoing));
    assert(chat::sameId(first,outgoing));
    assert(outgoing.sender==deviceId() && !strcmp(outgoing.name,"Bob"));
    txResult=1; loop(); assert(awaiting);
    deliverAck(outgoing,deviceId()); assert(awaiting); // own acknowledgement ignored
    deliverAck(message(9)); assert(awaiting);          // acknowledgement of another message
    deliverAck(outgoing); assert(!awaiting && view==View::Inbox && state.count==1 && !draft[0]);
    assert(strstr(lastScreen.lines[0],"Delivered"));
    const int chimesBefore=chimes;
    deliver(message()); assert(state.count==2 && chimes==chimesBefore+1);
    assert(flushAck(message()));
    deliver(message()); assert(state.count==2 && chimes==chimesBefore+1); // duplicate ignored...
    assert(flushAck(message()));                                          // ...but acknowledged again
    key(Key::Character,'x'); deliver(message(2)); assert(!strcmp(draft,"x") && view==View::Compose);
    assert(flushAck(message(2)));
    key(Key::Back); assert(view==View::Inbox && !strcmp(draft,"x")); // Wio Back leaves the editor, keeping the draft
    auto longMessage=message(3); memset(longMessage.text,'a',160); longMessage.text[160]=0;
    deliver(longMessage); assert(flushAck(longMessage)); selected=state.count-1; page=0;
    key(Key::Right); assert(page==1); key(Key::Right); assert(page==1 && view==View::Inbox);
    key(Key::Left); assert(page==0);
    key(Key::Select); key(Key::Character,'y'); assert(view==View::Compose && !strcmp(draft,"xy"));
    // Power loss while updating a slot: previous complete snapshot recovers.
    const int goodSlot=activeSlot; chat::State before;
    assert(chat::restore(files[goodSlot].data(),files[goodSlot].size(),before));
    failWrite=true; deliver(message(4)); failWrite=false;
    assert(activeSlot==goodSlot);
    chat::State recovered; assert(chat::restore(files[goodSlot].data(),files[goodSlot].size(),recovered));
    assert(recovered.count==before.count);
    assert(!chat::restore(files[1-goodSlot].data(),files[1-goodSlot].size(),recovered));
    assert(flushAck(message(4)));
    // Queue expiry keeps text available to retry.
    channelBusy=true; key(Key::Send); fakeTime+=91000; loop();
    assert(!queued && !sending && draft[0]);
    // A channel that always reads busy cannot block sending: after MaxBusyChecks it transmits anyway.
    key(Key::Send);
    for(unsigned i=0;i<=MaxBusyChecks && !sending;++i) { fakeTime+=1600; loop(); }
    assert(sending && busyChecks==MaxBusyChecks);
    channelBusy=false;
    txResult=1; loop(); fakeTime+=AckWindowMs+1; loop();
    assert(!awaiting && view==View::Compose && draft[0]);
    // The display sleeps after 10 s without input; the waking press is not applied.
    fakeTime+=ScreenTimeoutMs+1; loop(); assert(!displayOn && !screenPower);
    const size_t length=strlen(draft);
    nextInput={Key::Character,'z'}; loop(); assert(displayOn && screenPower && strlen(draft)==length);
    // A received message turns the display on.
    fakeTime+=ScreenTimeoutMs+1; loop(); assert(!displayOn);
    deliver(message(5)); assert(displayOn && screenPower);
    puts("PASS: onboarding, save failure, send/ack/no-ack/resend, receive acks and chime, paging, editor exit, recovery, display sleep");
}
int main() { protocolTests(); appTests(); puts("All tests passed."); }
