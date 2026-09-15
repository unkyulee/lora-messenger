#ifdef DEVICE_PAGER
#include <Arduino.h>
#include <LilyGoLib.h>
#include <Adafruit_GFX.h>
#include <LittleFS.h>
#include <esp_random.h>
#include "device.h"

static GFXcanvas16* canvas=nullptr;
static bool center=false;
static bool held=false;
static uint32_t pressedAt=0;
bool deviceBegin() {
    Serial.begin(115200);
    uint32_t found=instance.begin(NO_HW_GPS|NO_HW_NFC|NO_HW_SD|NO_HW_MIC|
        NO_HW_CODEC|NO_HW_SENSOR|NO_HW_RTC|NO_HW_LORA|NO_INIT_FATFS|NO_INIT_DELAY);
    instance.setRotation(1); instance.setBrightness(10);
    instance.kb.setRepeat(false);
    canvas=new GFXcanvas16(480,222);
    return canvas && canvas->getBuffer() && (found & HW_KEYBOARD_ONLINE);
}
void deviceTick() { instance.loop(); }
Input deviceInput() {
    char c=0;
    if(instance.kb.getKey(&c)==KB_PRESSED) {
        if(c=='\n' || c=='\r') return {Key::Send,0};
        if(c==8 || c==127) return {Key::Back,0};
        if(c==27) return {Key::Left,0};
        if(c=='\t') return {Key::Quick,0};
        if(c>=32 && c<=126) return {Key::Character,c};
    }
    RotaryMsg_t rotary=instance.getRotary();
    if(rotary.centerBtnPressed && !center) { pressedAt=millis(); held=false; }
    bool click=!rotary.centerBtnPressed && center && !held;
    center=rotary.centerBtnPressed;
    if(center && !held && millis()-pressedAt>=650) { held=true; return {Key::Quick,0}; }
    if(click) return {Key::Select,0};
    if(rotary.dir==ROTARY_DIR_UP) return {Key::Up,0};
    if(rotary.dir==ROTARY_DIR_DOWN) return {Key::Down,0};
    return {};
}
int deviceColumns() { return 40; }
void deviceDraw(const Screen& s) {
    if(!canvas || !canvas->getBuffer()) return;
    canvas->fillScreen(0x0841); canvas->setTextSize(2); canvas->setTextWrap(false);
    for(int row=0;row<8;++row) {
        bool selected=s.highlight==row;
        if(selected) canvas->fillRect(0,row*27,480,25,0x07ff);
        canvas->setTextColor(selected ? 0x0841 : row==0 ? 0x07ff : 0xffff);
        canvas->setCursor(0,row*27+5); canvas->print(s.lines[row]);
    }
    // LilyGo's SPI driver expects pixels in wire byte order.
    canvas->byteSwap();
    instance.lockSPI(); instance.pushColors(0,0,480,222,canvas->getBuffer()); instance.unlockSPI();
}
uint64_t deviceId() { return ESP.getEfuseMac(); }
uint32_t deviceRandom() { return esp_random(); }
static const char* path(int slot) { return slot ? "/chat1" : "/chat0"; }
bool storageBegin() { return LittleFS.begin(true); }
size_t storageRead(int slot,uint8_t* p,size_t cap) {
    File file=LittleFS.open(path(slot),"r");
    if(!file) return 0;
    size_t n=file.size(); if(n>cap) return 0;
    return file.read(p,n)==n ? n : 0;
}
bool storageWrite(int slot,const uint8_t* p,size_t n) {
    File file=LittleFS.open(path(slot),"w"); if(!file) return false;
    size_t wrote=file.write(p,n); file.flush(); file.close(); return wrote==n;
}
#endif
