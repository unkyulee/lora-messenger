#ifdef DEVICE_WIO
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <InternalFileSystem.h>
#include "device.h"

static Adafruit_SSD1306 display(128,64,&Wire,-1);
static constexpr uint8_t pins[]={25,26,27,28,29,13};
static constexpr Key keys[]={Key::Up,Key::Down,Key::Left,Key::Right,Key::Select,Key::Back};
static bool raw[6]={}, stable[6]={};
static uint32_t changed[6]={}, repeated[6]={};
bool deviceBegin() {
    Serial.begin(115200);
    for(uint8_t p:pins) pinMode(p,INPUT_PULLUP);
    Wire.begin();
    return display.begin(SSD1306_SWITCHCAPVCC,0x3c);
}
void deviceTick() {}
Input deviceInput() {
    const uint32_t now=millis();
    for(int i=0;i<6;++i) {
        bool pressed=digitalRead(pins[i])==LOW;
        if(pressed!=raw[i]) { raw[i]=pressed; changed[i]=now; }
        if(pressed!=stable[i] && now-changed[i]>=30) {
            stable[i]=pressed; repeated[i]=now+450;
            if(pressed) return {keys[i],0};
        }
        if(i<4 && stable[i] && int32_t(now-repeated[i])>=0) {
            repeated[i]=now+140; return {keys[i],0};
        }
    }
    return {};
}
int deviceColumns() { return 21; }
void deviceDraw(const Screen& s) {
    display.clearDisplay(); display.setTextSize(1); display.setTextWrap(false);
    for(int row=0;row<8;++row) {
        if(s.highlight==row) display.fillRect(0,row*8,128,8,SSD1306_WHITE);
        display.setTextColor(s.highlight==row ? SSD1306_BLACK : SSD1306_WHITE);
        display.setCursor(0,row*8); display.print(s.lines[row]);
    }
    if(s.caretRow<8) display.fillRect(s.caretColumn*6,s.caretRow*8,6,8,SSD1306_INVERSE);
    display.display();
}
uint64_t deviceId() { return uint64_t(NRF_FICR->DEVICEID[0]) | uint64_t(NRF_FICR->DEVICEID[1])<<32; }
uint32_t deviceRandom() {
    uint32_t value=0;
    NRF_RNG->CONFIG=1; NRF_RNG->TASKS_START=1;
    for(int i=0;i<4;++i) {
        NRF_RNG->EVENTS_VALRDY=0;
        while(!NRF_RNG->EVENTS_VALRDY) yield();
        value=(value<<8)|NRF_RNG->VALUE;
    }
    NRF_RNG->TASKS_STOP=1; return value;
}
static const char* path(int slot) { return slot ? "/chat1" : "/chat0"; }
bool storageBegin() { return InternalFS.begin(); }
size_t storageRead(int slot,uint8_t* p,size_t cap) {
    Adafruit_LittleFS_Namespace::File file(InternalFS);
    if(!file.open(path(slot),Adafruit_LittleFS_Namespace::FILE_O_READ)) return 0;
    size_t n=file.size();
    if(n>cap) { file.close(); return 0; }
    int read=file.read(p,n); file.close(); return read==int(n) ? n : 0;
}
bool storageWrite(int slot,const uint8_t* p,size_t n) {
    // Only remove the inactive slot: the other verified snapshot remains intact.
    InternalFS.remove(path(slot));
    Adafruit_LittleFS_Namespace::File file(InternalFS);
    if(!file.open(path(slot),Adafruit_LittleFS_Namespace::FILE_O_WRITE)) return false;
    size_t wrote=file.write(p,n); file.close(); return wrote==n;
}
#endif
