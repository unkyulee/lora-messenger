#ifdef DEVICE_PAGER
#include <Arduino.h>
#include <LilyGoLib.h>
#include <Adafruit_GFX.h>
#include <LittleFS.h>
#include <esp_random.h>
#include <esp_sleep.h>
#include <math.h>
#include <new>
#include "device.h"

#if !defined(CONFIG_SPIRAM_MODE_QUAD) || !CONFIG_SPIRAM_MODE_QUAD
#error "T-LoRa Pager requires quad SPI PSRAM: use qio_qspi in platformio.ini"
#endif

static GFXcanvas16* canvas=nullptr;
static uint32_t found=0;
static bool center=false, held=false, shift=false, sym=false;
static uint32_t pressedAt=0;
static Input keyEvent;
// Key numbers follow the physical layout (as in Meshtastic's TLoraPagerKeyboard):
// 0-9 Q-P, 10-18 A-L, 19 Enter, 20 Sym, 21-27 Z-M, 28 Shift, 29 Backspace, 30 Space.
// LilyGoLib's mapping treats Space as the symbol key and Sym as Alt, so raw events are decoded here.
static constexpr uint8_t EnterKey=19, SymKey=20, ShiftKey=28, BackspaceKey=29, KeyCount=31;
static constexpr char plainKeys[KeyCount]={
    'q','w','e','r','t','y','u','i','o','p',
    'a','s','d','f','g','h','j','k','l',0,
    0,'z','x','c','v','b','n','m',0,0,
    ' '};
static constexpr char symbolKeys[KeyCount]={
    '1','2','3','4','5','6','7','8','9','0',
    '*','/','+','-','=',':','\'','"','@',0,
    0,'_','$',';','?','!',',','.',0,0,
    ' '};
static void onRawKey(bool pressed,uint8_t raw) {
    if(!pressed || raw==0 || raw>KeyCount) return; // higher values are keypad GPIO events
    const uint8_t k=raw-1;
    // Shift and Sym apply to the next key; pressing one again cancels it.
    if(k==ShiftKey) { shift=!shift; keyEvent={Key::Modifier,0}; return; }
    if(k==SymKey) { sym=!sym; keyEvent={Key::Modifier,0}; return; }
    Input in;
    if(k==EnterKey) in={sym ? Key::Quick : Key::Send,0};
    else if(k==BackspaceKey) in={sym ? Key::Left : Key::Back,0};
    else {
        char c=(sym ? symbolKeys : plainKeys)[k];
        if(!c) return;
        if(shift && c>='a' && c<='z') c=char(c-32);
        in={Key::Character,c};
    }
    shift=sym=false; keyEvent=in;
}
bool deviceBegin() {
    Serial.begin(115200);
    // LilyGo's display initialization allocates a full framebuffer. Do not
    // enter it without external RAM: allocation failure otherwise aborts.
    if(!psramFound()) {
        Serial.println("Pager startup stopped: quad SPI PSRAM did not initialize.");
        return false;
    }
    // The codec is initialized for the receive chime; it stays closed, with the amplifier off, between chimes.
    found=instance.begin(NO_HW_GPS|NO_HW_NFC|NO_HW_SD|NO_HW_MIC|
        NO_HW_SENSOR|NO_HW_RTC|NO_HW_LORA|NO_INIT_FATFS|NO_INIT_DELAY);
    // LilyGo's rotation 0 is landscape (480x222); rotation 1 is portrait.
    instance.setRotation(0); instance.setBrightness(10);
    instance.powerControl(POWER_GPS,false);
    instance.powerControl(POWER_NFC,false);
    instance.powerControl(POWER_SD_CARD,false);
    instance.powerControl(POWER_SPEAK,false);
    instance.kb.setRepeat(false);
    instance.kb.setRawCallback(onRawKey);
    canvas=new (std::nothrow) GFXcanvas16(480,222);
    return canvas && canvas->getBuffer() && (found & HW_KEYBOARD_ONLINE);
}
void deviceTick() { instance.loop(); }
// Holding the wheel this long powers the Pager off.
static constexpr uint32_t PowerOffHoldMs=3000;
[[noreturn]] static void powerOff() {
    Serial.println("[pager] powering off");
    instance.wakeupDisplay(); instance.setBrightness(10);
    if(canvas && canvas->getBuffer()) {
        canvas->fillScreen(0x0841); canvas->setTextWrap(false);
        canvas->setTextSize(3); canvas->setTextColor(0x07ff);
        canvas->setCursor(12,50); canvas->print("Powering off");
        canvas->setTextSize(2); canvas->setTextColor(0xffff);
        canvas->setCursor(12,110); canvas->print("To turn on: hold PWR.");
        canvas->setCursor(12,140); canvas->print("On USB: press the wheel.");
        canvas->byteSwap(); instance.pushColors(0,0,480,222,canvas->getBuffer());
    }
    // Wait for release: a button still held would wake deep sleep at once.
    while(digitalRead(ROTARY_C)==LOW) delay(10);
    delay(1000);
    instance.setBrightness(0); instance.kb.setBrightness(0); instance.sleepDisplay();
    Serial.flush();
    // On battery, disconnecting the battery path turns the Pager off and the PWR button
    // turns it on; ppm.resetDefault() in instance.begin() reconnects it at boot.
    instance.ppm.shutdown();
    delay(1000);
    // Still running, so USB powers the board: sleep until the wheel or BOOT button,
    // which restarts the firmware.
    instance.powerControl(POWER_RADIO,false);
    instance.powerControl(POWER_SPEAK,false);
    instance.powerControl(POWER_HAPTIC_DRIVER,false);
    instance.powerControl(POWER_KEYBOARD,false);
    esp_sleep_enable_ext1_wakeup_io((1ULL<<ROTARY_C)|(1ULL<<0),ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
}
Input deviceInput() {
    keyEvent={};
    instance.kb.getKey(nullptr); // delivers at most one event to onRawKey
    if(keyEvent.key!=Key::None) return keyEvent;
    RotaryMsg_t rotary=instance.getRotary();
    if(rotary.centerBtnPressed && !center) { pressedAt=millis(); held=false; }
    bool click=!rotary.centerBtnPressed && center && !held;
    center=rotary.centerBtnPressed;
    if(center && millis()-pressedAt>=PowerOffHoldMs) powerOff();
    if(center && !held && millis()-pressedAt>=650) { held=true; return {Key::Quick,0}; }
    if(click) return {Key::Select,0};
    // Reversed so the wheel scrolls through messages in the expected direction.
    if(rotary.dir==ROTARY_DIR_UP) return {Key::Down,0};
    if(rotary.dir==ROTARY_DIR_DOWN) return {Key::Up,0};
    return {};
}
int deviceColumns() { return 40; }
const char* deviceModifier() { return sym ? "Sym" : shift ? "Shift" : ""; }
int deviceBattery() {
    if(!(found & HW_GAUGE_ONLINE) || !instance.gauge.refresh()) return -1;
    const int charge=instance.gauge.getStateOfCharge();
    return charge>100 ? 100 : charge;
}
void deviceChime() {
    if(!(found & HW_CODEC_ONLINE)) return;
    constexpr uint32_t rate=44100;
    struct Note { float hz; uint32_t ms; };
    // C7 then E7: the Pager's small speaker is most efficient around 2-3 kHz.
    static constexpr Note notes[]={{2093,90},{2637,260}};
    if(instance.codec.open(16,1,rate)<0) return;
    // The ES8311 ignores volume changes while closed, so set it after open().
    // 100 is the top of the codec's volume curve (0 dB).
    instance.codec.setVolume(100);
    int16_t chunk[256];
    const size_t capacity=sizeof(chunk)/sizeof(chunk[0]);
    // About 40 ms of silence first: open() has only just enabled the amplifier.
    memset(chunk,0,sizeof(chunk));
    for(int i=0;i<7;++i) instance.codec.write(reinterpret_cast<uint8_t*>(chunk),sizeof(chunk));
    constexpr float drive=1.8f;
    const float normalize=tanh(drive);
    for(const Note& note:notes) {
        const uint32_t total=rate*note.ms/1000;
        for(uint32_t i=0;i<total;) {
            size_t n=0;
            for(;n<capacity && i<total;++n,++i) {
                // Full scale with a 3 ms attack and slow decay; gentle tanh saturation
                // raises perceived loudness without the buzz of hard clipping.
                const float t=float(i)/rate, attack=t<0.003f ? t/0.003f : 1.0f;
                const float x=attack*exp(-t*6)*sin(2*PI*note.hz*t);
                chunk[n]=int16_t(32000*tanh(drive*x)/normalize);
            }
            instance.codec.write(reinterpret_cast<uint8_t*>(chunk),n*sizeof(int16_t));
        }
    }
    // Trailing silence flushes the last samples before close() turns the amplifier off.
    memset(chunk,0,sizeof(chunk));
    for(int i=0;i<6;++i) instance.codec.write(reinterpret_cast<uint8_t*>(chunk),sizeof(chunk));
    instance.codec.close();
}
void deviceDisplay(bool on) {
    if(on) {
        instance.wakeupDisplay(); delay(120); // ST7796 needs 120 ms after sleep-out
        instance.setBrightness(10); instance.kb.setBrightness(127);
    } else {
        instance.setBrightness(0); instance.kb.setBrightness(0); instance.sleepDisplay();
    }
}
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
    // pushColors takes the shared SPI mutex itself; that mutex is not
    // recursive, so wrapping this call in lockSPI() deadlocks the first draw.
    instance.pushColors(0,0,480,222,canvas->getBuffer());
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
