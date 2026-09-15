#ifdef DEVICE_WIO
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <InternalFileSystem.h>
#include "device.h"

// The Wio L1's 1.3" OLED is an SH1106. Its status register does not identify it
// reliably (reads 0x00, then 0x16), and the SSD1306 driver cannot address its rows.
static Adafruit_SH1106G display(128,64,&Wire,-1);
static bool displayReady=false;
static constexpr uint8_t pins[]={25,26,27,28,29,13};
static constexpr Key keys[]={Key::Up,Key::Down,Key::Left,Key::Right,Key::Select,Key::Back};
// Arduino pin numbers from the variant: buzzer P1.00, VBAT sense P0.31, divider enable P0.04.
static constexpr uint8_t BuzzerPin=12, BatteryPin=16, BatteryEnablePin=30;
static bool raw[6]={}, stable[6]={};
static uint32_t changed[6]={}, repeated[6]={};
struct Note { uint16_t hz, ms; };
static constexpr Note chime[]={{1760,70},{2637,150}};
static int chimeStep=-1;
static uint32_t chimeNext=0;
bool deviceBegin() {
    // LED: never lit = firmware not running; steady = stuck in setup; off once loop() runs.
    pinMode(LED_BUILTIN,OUTPUT); digitalWrite(LED_BUILTIN,HIGH);
    Serial.begin(115200);
    // USB serial drops output until a monitor opens the port; wait up to 5 s for one.
    for(uint32_t start=millis();!Serial && millis()-start<5000;) delay(10);
    Serial.println("\n[wio] boot");
    for(uint8_t p:pins) pinMode(p,INPUT_PULLUP);
    Wire.begin();
    Serial.print("[wio] I2C devices:"); Serial.flush();
    for(uint8_t a=1;a<127;++a) {
        Wire.beginTransmission(a);
        if(Wire.endTransmission()==0) Serial.printf(" 0x%02X",a);
    }
    Serial.println();
    // The L1 Pro's OLED answers at 0x3D; 0x3C is the other common address.
    uint8_t address=0;
    static constexpr uint8_t addresses[]={0x3c,0x3d};
    for(uint8_t candidate:addresses) {
        Wire.beginTransmission(candidate);
        if(Wire.endTransmission()==0) { address=candidate; break; }
    }
    if(!address) { Serial.println("[wio] no OLED acknowledged at 0x3C or 0x3D"); return false; }
    if(!display.begin(address,true)) { Serial.println("[wio] OLED initialization failed"); return false; }
    displayReady=true;
    // Show a frame now: storage and radio setup run before the first draw().
    display.clearDisplay(); display.setTextColor(SH110X_WHITE);
    display.setCursor(0,0); display.print("Starting..."); display.display();
    Serial.printf("[wio] SH1106 OLED ok at 0x%02X, showing Starting...\n",address);
    return true;
}
void deviceTick() {
    static bool ledOff=false;
    if(!ledOff) { digitalWrite(LED_BUILTIN,LOW); ledOff=true; }
    if(chimeStep>=0 && int32_t(millis()-chimeNext)>=0) {
        if(chimeStep<int(sizeof(chime)/sizeof(chime[0]))) {
            tone(BuzzerPin,chime[chimeStep].hz,chime[chimeStep].ms);
            chimeNext=millis()+chime[chimeStep].ms+10; ++chimeStep;
        } else { noTone(BuzzerPin); chimeStep=-1; }
    }
}
void deviceChime() { chimeStep=0; chimeNext=millis(); }
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
const char* deviceModifier() { return ""; }
int deviceBattery() {
    // The divider is only enabled while sampling (Meshtastic: ADC_CTRL active high).
    pinMode(BatteryEnablePin,OUTPUT); digitalWrite(BatteryEnablePin,HIGH); delay(10);
    analogReference(AR_INTERNAL); analogReadResolution(12);
    uint32_t sum=0;
    for(int i=0;i<4;++i) sum+=analogRead(BatteryPin);
    digitalWrite(BatteryEnablePin,LOW);
    // 3.6 V reference, 12 bits, 1:2 divider.
    const int mv=int(sum*3600u*2u/(4u*4095u));
    // LiPo open-circuit voltage for 0%..100% in 10% steps (Meshtastic's default curve).
    static constexpr int curve[]={3100,3300,3420,3530,3630,3720,3800,3890,3990,4050,4190};
    if(mv<=curve[0]) return 0;
    if(mv>=curve[10]) return 100;
    int i=1;
    while(mv>curve[i]) ++i;
    return (i-1)*10+10*(mv-curve[i-1])/(curve[i]-curve[i-1]);
}
void deviceDisplay(bool on) {
    if(displayReady) display.oled_command(on ? SH110X_DISPLAYON : SH110X_DISPLAYOFF);
}
void deviceDraw(const Screen& s) {
    if(!displayReady) return;
    display.clearDisplay(); display.setTextSize(1); display.setTextWrap(false);
    for(int row=0;row<8;++row) {
        if(s.highlight==row) display.fillRect(0,row*8,128,8,SH110X_WHITE);
        display.setTextColor(s.highlight==row ? SH110X_BLACK : SH110X_WHITE);
        display.setCursor(0,row*8); display.print(s.lines[row]);
    }
    if(s.caretRow<8) display.fillRect(s.caretColumn*6,s.caretRow*8,6,8,SH110X_INVERSE);
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
