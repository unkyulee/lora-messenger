#include <Arduino.h>
#include <stdio.h>
#include "chat.h"
#include "device.h"

namespace {
chat::State state;
chat::Message pending;
uint8_t snapshot[chat::SnapshotMax], packet[chat::PacketMax];
enum class View { Name, Inbox, Compose, Replies };
View view=View::Name;
char draft[chat::TextMax+1]={}, notice[41]={};
size_t selected=0, page=0, reply=0, keyboard=0;
uint32_t session=0, sequence=0, noticeUntil=0, retryAt=0, queuedAt=0;
int activeSlot=-1;
bool storageOK=false, radioOK=false, displayOK=false, dirty=true;
bool queued=false, sending=false, upper=false;
unsigned unread=0;
chat::AirtimeGate gate;
const char* replies[]={"Ciao!", "OK", "Sto arrivando", "Dove sei?", "Ci sono", "A dopo!"};
constexpr char keymap[]="abcdefghijklmnopqrstuvwxyz0123456789_<^>";

void notify(const char* text) {
    snprintf(notice,sizeof(notice),"%s",text); noticeUntil=millis()+4000; dirty=true;
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
    if(queued || sending) return;
    chat::trim(draft);
    if(!draft[0]) { notify(view==View::Name ? "Enter your name" : "Write a message first"); return; }
    if(view==View::Name) {
        memcpy(state.name,draft,strlen(draft)+1);
        if(!persist()) { state.name[0]=0; notify("Could not save name"); return; }
        draft[0]=0; view=View::Inbox; dirty=true; return;
    }
    if(!radioOK) { notify("Radio unavailable"); return; }
    pending=chat::Message{}; pending.sender=deviceId(); pending.session=session;
    pending.sequence=++sequence;
    if(!sequence) { session=deviceRandom()|1u; pending.session=session; pending.sequence=++sequence; }
    memcpy(pending.name,state.name,sizeof(state.name));
    memcpy(pending.text,draft,strlen(draft)+1);
    queued=true; queuedAt=millis(); retryAt=millis()+100+(deviceRandom()%500); dirty=true;
}
void input(Input in) {
    if(in.key==Key::None) return;
    if(queued || sending) {
        if(queued && (in.key==Key::Back || in.key==Key::Quick)) { queued=false; notify("Send cancelled"); }
        return;
    }
    notice[0]=0; dirty=true;
    if(view==View::Inbox) {
        if(in.key==Key::Up && selected>0) { --selected; page=0; }
        if(in.key==Key::Down && selected+1<state.count) { ++selected; page=0; }
        if(in.key==Key::Left && page) --page;
        if(in.key==Key::Right) {
            size_t pages=state.count ? (strlen(state.messages[selected].text)+deviceColumns()*4-1)/(deviceColumns()*4) : 1;
            if(page+1<pages) ++page;
            else { view=View::Replies; reply=0; }
        }
        if(in.key==Key::Quick) { view=View::Replies; reply=0; }
        if(in.key==Key::Select || in.key==Key::Send || in.key==Key::Character) {
            view=View::Compose; keyboard=0;
            if(in.key==Key::Character) append(in.character);
        }
        if(state.count && selected==state.count-1) unread=0;
        return;
    }
    if(view==View::Replies) {
        if(in.key==Key::Up) reply=(reply+5)%6;
        if(in.key==Key::Down) reply=(reply+1)%6;
        if(in.key==Key::Back || in.key==Key::Left || in.key==Key::Quick) { view=View::Inbox; return; }
        if(in.key==Key::Select || in.key==Key::Send) {
            snprintf(draft,sizeof(draft),"%s",replies[reply]); view=View::Compose;
            keyboard=39; // Send is selected; one further click confirms the reply.
        }
        return;
    }
    if(in.key==Key::Character) { append(in.character); return; }
    if(in.key==Key::Send) { submit(); return; }
    if(in.key==Key::Back) {
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
void draw() {
    Screen screen;
    const size_t cols=deviceColumns();
    if(view==View::Inbox) {
        line(screen,0,"EVERYONE");
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
            snprintf(screen.lines[6],cols+1,"%u/%u  p%u/%u +%u",unsigned(selected+1),unsigned(state.count),
                unsigned(page+1),unsigned((len+cols*4-1)/(cols*4)),unread);
        }
#ifdef DEVICE_PAGER
        line(screen,7,"Type:write Wheel:read Hold:replies");
#else
        line(screen,7,"OK:write R:more");
#endif
    } else if(view==View::Replies) {
        line(screen,0,"QUICK REPLIES");
        for(int i=0;i<6;++i) line(screen,i+1,replies[i]);
        screen.highlight=uint8_t(reply+1); line(screen,7,"OK:choose Back:close");
    } else {
        line(screen,0,view==View::Name ? "WHAT'S YOUR NAME?" : "TO EVERYONE");
        size_t len=strlen(draft), start=len>cols*2-1 ? len-(cols*2-1) : 0;
        for(int row=1;row<3;++row) {
            size_t n=len-start<cols ? len-start : cols;
            memcpy(screen.lines[row],draft+start,n); start+=n;
            if(start==len) { if(n<cols) screen.lines[row][n]='_'; break; }
        }
#ifdef DEVICE_PAGER
        line(screen,4,view==View::Name ? "Choose the name others will see." : "Everyone nearby can read this.");
        snprintf(screen.lines[6],cols+1,"%u/%u characters",unsigned(len),unsigned(view==View::Name ? chat::NameMax : chat::TextMax));
        line(screen,7,view==View::Name ? "Enter:save name" : "Enter:send  Hold wheel:back");
#else
        for(int row=0;row<4;++row) for(int col=0;col<10;++col) {
            char c=keymap[row*10+col];
            screen.lines[row+3][col*2]=upper && c>='a' && c<='z' ? c-32 : c;
            if(col<9) screen.lines[row+3][col*2+1]=' ';
        }
        screen.caretRow=uint8_t(keyboard/10+3); screen.caretColumn=uint8_t((keyboard%10)*2);
        const char* action=keyboard==36 ? "OK:space" : keyboard==37 ? "OK:erase" : keyboard==38 ? "OK:ABC/abc" : keyboard==39 ? (view==View::Name ? "OK:save name" : "OK:send to everyone") : "OK:letter Back:erase";
        line(screen,7,action);
#endif
    }
    if(queued || sending) {
        line(screen,0,sending ? "SENDING..." : "WAITING TO SEND...");
        line(screen,7,sending ? "" : "Back:cancel");
    } else if(notice[0]) line(screen,7,notice);
    else if(!radioOK) line(screen,7,"Radio unavailable");
    else if(!storageOK) line(screen,7,"Storage unavailable");
    deviceDraw(screen); dirty=false;
}
}

void setup() {
    displayOK=deviceBegin();
    if(!displayOK) {
        Serial.println("Startup stopped: display/input initialization failed.");
        while(true) delay(250);
    }
    storageOK=storageBegin();
    static chat::State candidate;
    if(storageOK) for(int slot=0;slot<2;++slot) {
        size_t n=storageRead(slot,snapshot,sizeof(snapshot));
        if(chat::restore(snapshot,n,candidate) && (activeSlot<0 || chat::newer(candidate.generation,state.generation))) {
            state=candidate; activeSlot=slot;
        }
    }
    session=deviceRandom()|1u;
    view=state.name[0] ? View::Inbox : View::Name;
    if(state.count) selected=state.count-1;
    radioOK=radioBegin();
    // Even repeated power cycles cannot bypass the inter-transmission pause.
    gate.next=millis()+(radioOK ? radioAirtime(chat::PacketMax)*20+100 : 30000);
    if(!displayOK) Serial.println("Display/input initialization failed");
    draw();
}
void loop() {
    deviceTick(); input(deviceInput());
    const uint32_t now=millis();
    if(notice[0] && chat::due(now,noticeUntil)) { notice[0]=0; dirty=true; }
    if(radioOK) {
        int result=radioResult();
        if(sending && result) {
            sending=false;
            if(result>0) {
                state.add(pending); selected=state.count-1; page=0; unread=0;
                draft[0]=0; view=View::Inbox;
                notify(persist() ? "Broadcast sent" : "Sent; history unsaved");
            } else notify("Send failed; try again");
        }
        int n=radioReceive(packet,sizeof(packet));
        chat::Message incoming;
        if(n>0 && chat::decode(packet,size_t(n),incoming) && incoming.sender!=deviceId()) {
            bool atEnd=!state.count || selected==state.count-1;
            bool wasFull=state.count==chat::HistoryMax;
            if(state.add(incoming)) {
                if(view==View::Inbox && atEnd) { selected=state.count-1; page=0; }
                else {
                    ++unread;
                    if(wasFull) { if(selected) --selected; else page=0; }
                }
                if(!persist()) notify("History not saved");
                dirty=true;
            }
        }
        if(queued && now-queuedAt>90000) { queued=false; notify("Channel busy; retry"); }
        if(queued && gate.ready(now) && chat::due(now,retryAt)) {
            if(radioBusy()) retryAt=now+300+deviceRandom()%1200;
            else {
                size_t length=chat::encode(pending,packet,sizeof(packet));
                gate.charge(now,radioAirtime(length)); queued=false;
                sending=length && radioSend(packet,length);
                if(!sending) notify("Send failed; try again");
                dirty=true;
            }
        }
    }
    if(dirty) draw();
    delay(5);
}
