#ifndef DEVICE_RELAY
#include <Arduino.h>
#include <stdio.h>
#include "chat.h"
#include "network.h"
#include "device.h"

namespace {
constexpr uint32_t NoticeMs=4000, ScreenTimeoutMs=10000, BatteryPeriodMs=30000;
// After its transmission completes, a sender listens this long for any receiver's acknowledgement.
constexpr uint32_t AckWindowMs=network::AckWindow;
// Channel activity detection is a courtesy; the airtime budget bounds transmissions.
// After this many busy results, send anyway so a noisy receiver front end cannot block sending.
constexpr unsigned MaxBusyChecks=5, MaxAckBusyChecks=2;
chat::State state;
chat::Message pending;
uint8_t snapshot[chat::SnapshotMax], packet[network::PacketMax], outgoing[network::PacketMax];
size_t outgoingLength=0;
enum class View { Name, Inbox, Compose };
enum class Tx { None, Message, Ack };
View view=View::Name;
Tx tx=Tx::None;
char draft[chat::TextMax+1]={}, notice[41]={}, status[24]={};
size_t selected=0, page=0, keyboard=0;
uint32_t session=0, sequence=0, noticeUntil=0, statusUntil=0, retryAt=0, queueExpires=0;
uint32_t ackDeadline=0, lastInput=0, batteryAt=0;
int activeSlot=-1, battery=-1;
unsigned busyChecks=0;
bool storageOK=false, radioOK=false, displayOK=false, dirty=true, displayOn=true;
// retryable: `pending` is undelivered, so resending unchanged text reuses its identity.
bool queued=false, sending=false, awaiting=false, retryable=false, upper=false;
const char* modifier="";
unsigned unread=0;
chat::AirtimeGate gate;
struct PendingAck { chat::Ack ack; uint8_t attempt=0; uint32_t due=0, expires=0; unsigned busyChecks=0; bool used=false; };
PendingAck acks[4];
network::Seen<64> acknowledged;
uint8_t attempt=0;
uint32_t deliveryDeadline=0;
size_t encodePending() { network::Frame f; f.message=pending; f.attempt=attempt; return network::encode(f,outgoing,sizeof(outgoing)); }
constexpr char keymap[]="abcdefghijklmnopqrstuvwxyz0123456789_<^>";

void notify(const char* text) {
    snprintf(notice,sizeof(notice),"%s",text); noticeUntil=millis()+NoticeMs; dirty=true;
}
void setStatus(const char* text) {
    snprintf(status,sizeof(status),"%s",text); statusUntil=millis()+NoticeMs; dirty=true;
}
void wake() {
    lastInput=millis();
    if(!displayOn) { displayOn=true; deviceDisplay(true); dirty=true; }
}
bool persist() {
    if(!storageOK) return false;
    ++state.generation;
    size_t n=chat::save(state,snapshot,sizeof(snapshot));
    int target=activeSlot==0 ? 1 : 0;
    if(!n || !storageWrite(target,snapshot,n)) return false;
    // Verify the committed file before retiring the previous slot.
    size_t read=storageRead(target,snapshot,sizeof(snapshot));
    static chat::State checked;
    if(!chat::restore(snapshot,read,checked) || checked.generation!=state.generation) return false;
    activeSlot=target; return true;
}
void append(char c) {
    size_t n=strlen(draft), limit=view==View::Name ? chat::NameMax : chat::TextMax;
    if(n<limit) { draft[n]=c; draft[n+1]=0; dirty=true; }
    else notify(view==View::Name ? "Name: 16 characters" : "Message is full");
}
void submit() {
    if(queued || sending || awaiting) return;
    chat::trim(draft);
    if(!draft[0]) { notify(view==View::Name ? "Enter your name" : "Write a message first"); return; }
    if(view==View::Name) {
        memcpy(state.name,draft,strlen(draft)+1);
        if(!persist()) { state.name[0]=0; notify("Could not save name"); return; }
        draft[0]=0; view=View::Inbox; dirty=true; return;
    }
    if(!radioOK) { notify("Radio unavailable"); return; }
    // Within a delivery, unchanged text keeps its identity. An explicit send
    // after exhaustion/expiry starts a new delivery.
    if(!retryable || strcmp(pending.text,draft) || attempt+1>=network::Attempts || chat::due(millis(),deliveryDeadline)) {
        attempt=0; deliveryDeadline=millis()+network::Lifetime;
        pending=chat::Message{}; pending.sender=deviceId(); pending.session=session;
        pending.sequence=++sequence;
        if(!sequence) { session=deviceRandom()|1u; pending.session=session; pending.sequence=++sequence; }
        memcpy(pending.name,state.name,sizeof(state.name));
        memcpy(pending.text,draft,strlen(draft)+1);
    }
    else ++attempt;
    outgoingLength=encodePending();
    if(!outgoingLength) { notify("Message cannot be sent"); return; }
    gate.update(millis());
    Serial.printf("[send] queued %u bytes, airtime %u ms, credit %u ms\n",unsigned(outgoingLength),
        unsigned(radioAirtime(outgoingLength)),unsigned(gate.credit/20));
    retryable=true; busyChecks=0;
    queued=true; queueExpires=deliveryDeadline; retryAt=millis()+100+(deviceRandom()%500); dirty=true;
}
void sendFailed(const char* why) {
    // Back to the editor with the text kept, ready to change or resend.
    view=View::Compose; keyboard=39; notify(why); setStatus("Delivery unconfirmed"); wake();
}
void delivered() {
    awaiting=false; queued=false; retryable=false;
    state.add(pending); selected=state.count-1; page=0; unread=0;
    draft[0]=0; view=View::Inbox;
    if(!persist()) notify("Sent; history unsaved");
    setStatus("Delivered"); wake();
}
void scheduleAck(const chat::Message& m,uint32_t now,uint8_t receivedAttempt=0) {
    PendingAck* slot=nullptr;
    for(auto& a:acks) {
        if(a.used && a.ack.sender==m.sender && a.ack.session==m.session && a.ack.sequence==m.sequence) return;
        if(!a.used && !slot) slot=&a;
    }
    if(!slot) return;
    network::Frame seen; seen.message=m; seen.attempt=receivedAttempt;
    if(!acknowledged.insert(seen,now)) return;
    slot->attempt=receivedAttempt;
    slot->ack=chat::Ack{}; slot->ack.sender=m.sender; slot->ack.session=m.session;
    slot->ack.sequence=m.sequence; slot->ack.from=deviceId();
    // A random delay spreads replies from several receivers; the sender needs only one.
    slot->due=now+150+deviceRandom()%900;
    slot->expires=now+AckWindowMs-radioAirtime(chat::AckSize+network::Overhead)-200;
    slot->busyChecks=0; slot->used=true;
}
void serviceAcks(uint32_t now) {
    if(tx!=Tx::None) return;
    const uint32_t air=radioAirtime(chat::AckSize+network::Overhead);
    for(auto& a:acks) {
        if(!a.used || !chat::due(now,a.due)) continue;
        if(chat::due(now,a.expires)) { a.used=false; continue; }
        if(!gate.ready(now,air)) continue;
        if(a.busyChecks<MaxAckBusyChecks && radioBusy()) { ++a.busyChecks; a.due=now+200+deviceRandom()%600; continue; }
        a.used=false;
        network::Frame f; f.ack=true; f.receipt=a.ack; f.attempt=a.attempt;
        size_t n=network::encode(f,packet,sizeof(packet));
        gate.charge(now,air);
        if(n && radioSend(packet,n)) tx=Tx::Ack;
        return;
    }
}
void input(Input in) {
    if(in.key==Key::None || in.key==Key::Modifier) return;
    if(queued || sending || awaiting) {
        if(queued && (in.key==Key::Back || in.key==Key::Quick)) { queued=false; notify("Send cancelled"); }
        return;
    }
    notice[0]=0; dirty=true;
    if(view==View::Inbox) {
        if(in.key==Key::Up && selected>0) { --selected; page=0; }
        if(in.key==Key::Down && selected+1<state.count) { ++selected; page=0; }
        if(in.key==Key::Left && page) --page;
        if(in.key==Key::Right && state.count) {
            size_t pages=(strlen(state.messages[selected].text)+deviceColumns()*4-1)/(deviceColumns()*4);
            if(page+1<pages) ++page;
        }
        if(in.key==Key::Select || in.key==Key::Send || in.key==Key::Character) {
            view=View::Compose; keyboard=0;
            if(in.key==Key::Character) append(in.character);
        }
        if(state.count && selected==state.count-1) unread=0;
        return;
    }
    if(in.key==Key::Character) { append(in.character); return; }
    if(in.key==Key::Send) { submit(); return; }
    if(in.key==Key::Back) {
#ifndef DEVICE_PAGER
        // Wio: the user button leaves the editor and keeps the draft; '<' erases.
        if(view==View::Compose) { view=View::Inbox; return; }
#endif
        size_t n=strlen(draft);
        if(n) draft[n-1]=0;
        else if(view==View::Compose) view=View::Inbox;
        return;
    }
#ifdef DEVICE_PAGER
    if(in.key==Key::Quick && view==View::Compose) { view=View::Inbox; return; }
    if(in.key==Key::Left && view==View::Compose) { view=View::Inbox; return; }
    if(in.key==Key::Select) { submit(); return; }
#endif
    if(in.key==Key::Up) keyboard=(keyboard+30)%40;
    if(in.key==Key::Down) keyboard=(keyboard+10)%40;
    if(in.key==Key::Left) keyboard=(keyboard/10)*10+(keyboard+9)%10;
    if(in.key==Key::Right) keyboard=(keyboard/10)*10+(keyboard+1)%10;
    if(in.key==Key::Select) {
        char c=keymap[keyboard];
        if(c=='>') submit();
        else if(c=='^') upper=!upper;
        else if(c=='<') { size_t n=strlen(draft); if(n) draft[n-1]=0; }
        else append(c=='_' ? ' ' : upper && c>='a' && c<='z' ? c-32 : c);
    }
}
void line(Screen& s,int row,const char* value) {
    snprintf(s.lines[row],size_t(deviceColumns())+1,"%s",value);
}
// Row 0: activity or view name on the left, modifier and battery on the right.
void statusBar(Screen& s,const char* label) {
    const size_t cols=deviceColumns();
    char right[16];
    if(battery>=0) snprintf(right,sizeof(right),"%s%s%d%%",modifier,modifier[0] ? " " : "",battery);
    else snprintf(right,sizeof(right),"%s",modifier);
    size_t rl=strlen(right), ll=strlen(label);
    if(rl>cols) rl=cols;
    if(ll+rl+1>cols) ll=cols>rl+1 ? cols-rl-1 : 0;
    memset(s.lines[0],' ',cols); s.lines[0][cols]=0;
    memcpy(s.lines[0],label,ll);
    memcpy(s.lines[0]+cols-rl,right,rl);
}
void draw() {
    Screen screen;
    const size_t cols=deviceColumns();
    const char* label=view==View::Name ? "Your name?" : view==View::Inbox ? "Messages" : "Message";
    if(view==View::Inbox) {
        if(!state.count) {
            line(screen,2,"No messages yet"); line(screen,3,"Say hello nearby.");
        } else {
            const auto& m=state.messages[selected];
            snprintf(screen.lines[1],cols+1,"%s%s",m.sender==deviceId() ? "You: " : "",m.name);
            size_t len=strlen(m.text), offset=page*cols*4;
            for(int row=2;row<6 && offset<len;++row) {
                size_t n=len-offset<cols ? len-offset : cols;
                memcpy(screen.lines[row],m.text+offset,n); offset+=n;
            }
            int used=snprintf(screen.lines[6],cols+1,"%u/%u  p%u/%u",unsigned(selected+1),unsigned(state.count),
                unsigned(page+1),unsigned((len+cols*4-1)/(cols*4)));
            // The unread count only appears when messages arrived while reading older ones.
            if(unread && used>=0 && size_t(used)<cols)
                snprintf(screen.lines[6]+used,cols+1-used," +%u",unread);
        }
#ifdef DEVICE_PAGER
        line(screen,7,"Type:write Wheel:read");
#else
        line(screen,7,"OK:write R:more");
#endif
    } else {
        size_t len=strlen(draft), start=len>cols*2-1 ? len-(cols*2-1) : 0;
        for(int row=1;row<3;++row) {
            size_t n=len-start<cols ? len-start : cols;
            memcpy(screen.lines[row],draft+start,n); start+=n;
            if(start==len) { if(n<cols) screen.lines[row][n]='_'; break; }
        }
#ifdef DEVICE_PAGER
        if(view==View::Name) line(screen,4,"Choose the name others will see.");
        snprintf(screen.lines[6],cols+1,"%u/%u characters",unsigned(len),unsigned(view==View::Name ? chat::NameMax : chat::TextMax));
        line(screen,7,view==View::Name ? "Enter:save name" : "Enter:send  Hold wheel:back");
#else
        for(int row=0;row<4;++row) for(int col=0;col<10;++col) {
            char c=keymap[row*10+col];
            screen.lines[row+3][col*2]=upper && c>='a' && c<='z' ? c-32 : c;
            if(col<9) screen.lines[row+3][col*2+1]=' ';
        }
        screen.caretRow=uint8_t(keyboard/10+3); screen.caretColumn=uint8_t((keyboard%10)*2);
        const char* action=keyboard==36 ? "OK:space" : keyboard==37 ? "OK:erase" : keyboard==38 ? "OK:ABC/abc" :
            keyboard==39 ? (view==View::Name ? "OK:save name" : "OK:send to everyone") :
            view==View::Name ? "OK:letter Back:erase" : "OK:letter Back:exit";
        line(screen,7,action);
#endif
    }
    if(queued) label="Waiting to send";
    else if(sending) label="Sending...";
    else if(awaiting) label="Waiting for ack";
    else if(status[0]) label=status;
    statusBar(screen,label);
    if(queued) line(screen,7,"Back:cancel");
    else if(sending || awaiting) line(screen,7,"");
    else if(notice[0]) line(screen,7,notice);
    else if(!radioOK) line(screen,7,"Radio unavailable");
    else if(!storageOK) line(screen,7,"Storage unavailable");
    deviceDraw(screen); dirty=false;
}
}

void setup() {
    displayOK=deviceBegin();
    if(!displayOK) {
        // Repeat: USB serial drops output sent before a monitor is attached.
        while(true) { Serial.println("Startup stopped: display/input initialization failed."); delay(2000); }
    }
    Serial.printf("[boot] display/input ok, starting storage\n");
    storageOK=storageBegin();
    static chat::State candidate;
    if(storageOK) for(int slot=0;slot<2;++slot) {
        size_t n=storageRead(slot,snapshot,sizeof(snapshot));
        if(chat::restore(snapshot,n,candidate) && (activeSlot<0 || chat::newer(candidate.generation,state.generation))) {
            state=candidate; activeSlot=slot;
        }
    }
    Serial.printf("[boot] storage %s, slot %d, %u messages\n",storageOK ? "ok" : "FAILED",activeSlot,unsigned(state.count));
    session=deviceRandom()|1u;
    view=state.name[0] ? View::Inbox : View::Name;
    if(state.count) selected=state.count-1;
    Serial.printf("[boot] starting radio\n");
    radioOK=radioBegin();
    Serial.printf("[boot] radio %s, drawing first screen\n",radioOK ? "ok" : "FAILED");
    // Airtime credit starts empty, so power cycling cannot bypass the budget.
    gate.start(millis());
    battery=deviceBattery(); batteryAt=millis()+BatteryPeriodMs; lastInput=millis();
    draw();
}
void loop() {
    deviceTick();
    Input in=deviceInput();
    if(in.key!=Key::None) {
        const bool asleep=!displayOn;
        wake();
        if(!asleep) input(in); // the press that wakes the display is not acted on
    }
    // Read the clock after input: submit() stamps its deadlines with millis(), and a
    // `now` taken earlier (the Pager's I2C keyboard read takes milliseconds) would be
    // behind them.
    const uint32_t now=millis();
    if(notice[0] && chat::due(now,noticeUntil)) { notice[0]=0; dirty=true; }
    if(status[0] && chat::due(now,statusUntil)) { status[0]=0; dirty=true; }
    if(chat::due(now,batteryAt)) {
        batteryAt=now+BatteryPeriodMs;
        const int level=deviceBattery();
        if(level!=battery) { battery=level; dirty=true; }
    }
    const char* activeModifier=deviceModifier();
    if(activeModifier!=modifier) { modifier=activeModifier; dirty=true; }
    if(radioOK) {
        if(tx!=Tx::None) {
            int result=radioResult();
            if(result) {
                if(tx==Tx::Message) {
                    sending=false;
                    Serial.printf("[send] transmission %s\n",result>0 ? "complete, waiting for ack" : "FAILED");
                    if(result>0) { awaiting=true; ackDeadline=now+AckWindowMs; }
                    else sendFailed("Send failed; try again");
                }
                tx=Tx::None; dirty=true;
            }
        }
        int n=radioReceive(packet,sizeof(packet));
        network::Frame frame;
        const bool valid=n>0 && network::decode(packet,size_t(n),frame);
        const auto& incoming=frame.message;
        const auto& ack=frame.receipt;
        if(valid && !frame.ack && incoming.sender!=deviceId()) {
            // A new attempt may need an ACK even when the message is already stored.
            bool atEnd=!state.count || selected==state.count-1;
            bool wasFull=state.count==chat::HistoryMax;
            if(state.add(incoming)) {
                if(view==View::Inbox && atEnd) { selected=state.count-1; page=0; }
                else {
                    ++unread;
                    if(wasFull) { if(selected) --selected; else page=0; }
                }
                setStatus("New message"); wake(); deviceChime();
            }
            // Re-verify storage for a duplicate following an earlier write failure.
            if(persist()) scheduleAck(incoming,now,frame.attempt);
            else notify("History not saved");
        } else if(valid && frame.ack && (awaiting || queued) &&
                  ack.from!=deviceId() && chat::acknowledges(ack,pending)) {
            delivered();
        }
        if(awaiting && chat::due(now,ackDeadline)) {
            awaiting=false;
            if(attempt+1<network::Attempts && !chat::due(now,deliveryDeadline)) {
                ++attempt; outgoingLength=encodePending(); queued=true; busyChecks=0;
                retryAt=now+500+deviceRandom()%1500;
            } else sendFailed("Delivery unconfirmed");
        }
        if((queued || awaiting) && chat::due(now,deliveryDeadline)) {
            queued=false; awaiting=false; sendFailed("Delivery unconfirmed");
        }
        if(queued && chat::due(now,queueExpires)) { queued=false; notify("Channel busy; retry"); }
        serviceAcks(now);
        if(queued && tx==Tx::None && chat::due(now,retryAt)) {
            const uint32_t air=radioAirtime(outgoingLength);
            if(gate.ready(now,air)) {
                if(busyChecks<MaxBusyChecks && radioBusy()) {
                    ++busyChecks; retryAt=now+300+deviceRandom()%1200;
                } else {
                    if(busyChecks==MaxBusyChecks) Serial.printf("[send] channel busy %u times; sending anyway\n",busyChecks);
                    gate.charge(now,air); queued=false;
                    sending=radioSend(outgoing,outgoingLength);
                    if(sending) tx=Tx::Message;
                    else sendFailed("Send failed; try again");
                    dirty=true;
                }
            }
        }
    }
    if(displayOn && chat::due(now,lastInput+ScreenTimeoutMs)) { displayOn=false; deviceDisplay(false); }
    if(dirty && displayOn) draw();
    delay(5);
}

#endif
