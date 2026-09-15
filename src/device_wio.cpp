#ifdef DEVICE_WIO
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SH110X.h>
#include <InternalFileSystem.h>
#include "device.h"

// Wio L1 variants differ: the L1 Pro has an SH1106 at 0x3D. Buffers are only
// allocated by begin(), so keeping both drivers costs no framebuffer RAM.
static Adafruit_SSD1306 ssd1306(128,64,&Wire,-1);
static Adafruit_SH1106G sh1106(128,64,&Wire,-1);
static Adafruit_GFX* display=nullptr;
static bool useSH1106=false;
static constexpr uint8_t pins[]={25,26,27,28,29,13};
static constexpr Key keys[]={Key::Up,Key::Down,Key::Left,Key::Right,Key::Select,Key::Back};
static bool raw[6]={}, stable[6]={};
static uint32_t changed[6]={}, repeated[6]={};
static bool led=true;
static void clearScreen() { if(useSH1106) sh1106.clearDisplay(); else ssd1306.clearDisplay(); }
static void showScreen() { if(useSH1106) sh1106.display(); else ssd1306.display(); }
// Meshtastic's heuristic: a status-register low nibble of 0x08 or 0x00 is an SH1106.
static bool probeSH1106(uint8_t address) {
    uint8_t r=0, previous=0;
    int tries=0;
    do {
        previous=r;
        Wire.beginTransmission(address); Wire.write(uint8_t(0x00)); Wire.endTransmission();
        Wire.requestFrom(address,size_t(1));
        if(Wire.available()) r=Wire.read()&0x0f;
    } while(r!=previous && ++tries<4);
    Serial.printf("[wio] OLED status nibble 0x%X\n",r);
    return r==0x08 || r==0x00;
}
bool deviceBegin() {
    // LED: never lit = firmware not running; steady = stuck in setup; blinking = loop running.
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
    // Probe the panel first: Adafruit_SSD1306::begin never checks for an ACK.
    uint8_t address=0;
    static constexpr uint8_t addresses[]={0x3c,0x3d};
    for(uint8_t candidate:addresses) {
        Wire.beginTransmission(candidate);
        if(Wire.endTransmission()==0) { address=candidate; break; }
    }
    if(!address) { Serial.println("[wio] no OLED acknowledged at 0x3C or 0x3D"); return false; }
    useSH1106=probeSH1106(address);
    bool ok=useSH1106 ? sh1106.begin(address,true) : ssd1306.begin(SSD1306_SWITCHCAPVCC,address,true,false);
    if(!ok) { Serial.println("[wio] OLED initialization failed"); return false; }
    display=useSH1106 ? static_cast<Adafruit_GFX*>(&sh1106) : &ssd1306;
    // Show a frame now: storage and radio setup run before the first draw().
    clearScreen(); display->setTextColor(SSD1306_WHITE);
    display->setCursor(0,0); display->print("Starting..."); showScreen();
    Serial.printf("[wio] %s OLED ok at 0x%02X, showing Starting...\n",useSH1106 ? "SH1106" : "SSD1306",address);
    return true;
}
void deviceTick() {
    // Output pins read back as 0 on this core, so track the LED state here.
    static uint32_t toggled=0;
    if(millis()-toggled>=500) { toggled=millis(); led=!led; digitalWrite(LED_BUILTIN,led); }
}
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
    if(!display) return;
    // SSD1306_* and SH110X_* colors share the values 0 (black), 1 (white), 2 (inverse).
    clearScreen(); display->setTextSize(1); display->setTextWrap(false);
    for(int row=0;row<8;++row) {
        if(s.highlight==row) display->fillRect(0,row*8,128,8,SSD1306_WHITE);
        display->setTextColor(s.highlight==row ? SSD1306_BLACK : SSD1306_WHITE);
        display->setCursor(0,row*8); display->print(s.lines[row]);
    }
    if(s.caretRow<8) display->fillRect(s.caretColumn*6,s.caretRow*8,6,8,SSD1306_INVERSE);
    showScreen();
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
