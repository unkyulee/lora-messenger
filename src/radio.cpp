#include <Arduino.h>
#include <RadioLib.h>
#include "device.h"
#include "chat.h"
#ifdef DEVICE_PAGER
#include <LilyGoLib.h>
#else
#include <SPI.h>
#ifdef DEVICE_RELAY
static LR1110 radio=new Module(12,33,42,7);
#else
static SX1262 radio=new Module(4,1,2,3);
#endif
#endif

static volatile bool interrupt=false;
static bool transmitting=false;
static uint32_t started=0;
#ifdef DEVICE_PAGER
static void IRAM_ATTR onRadio() { interrupt=true; }
#else
static void onRadio() { interrupt=true; }
#endif
bool radioBegin() {
#ifdef DEVICE_PAGER
    if(!instance.initLoRa()) return false;
    if(radio.setTCXO(3.0)!=RADIOLIB_ERR_NONE) return false;
#elif defined(DEVICE_RELAY)
    SPI.begin();
    int result=radio.begin(869.525,125.0,9,5,0x12,14,8,1.6);
    Serial.printf("[relay] LR1110 begin: %d\n",result);
    if(result!=RADIOLIB_ERR_NONE) return false;
    static const uint32_t pins[Module::RFSWITCH_MAX_PINS]={RADIOLIB_LR11X0_DIO5,
        RADIOLIB_LR11X0_DIO6,RADIOLIB_LR11X0_DIO7,RADIOLIB_LR11X0_DIO8,RADIOLIB_NC};
    static const Module::RfSwitchMode_t modes[]={
        {LR11x0::MODE_STBY,{LOW,LOW,LOW,LOW}},
        {LR11x0::MODE_RX,{HIGH,LOW,LOW,HIGH}},
        {LR11x0::MODE_TX,{HIGH,HIGH,LOW,HIGH}},
        {LR11x0::MODE_TX_HP,{LOW,HIGH,LOW,HIGH}},
        {LR11x0::MODE_TX_HF,{LOW,LOW,LOW,LOW}},
        {LR11x0::MODE_GNSS,{LOW,LOW,HIGH,LOW}},
        {LR11x0::MODE_WIFI,{LOW,LOW,LOW,LOW}}, END_OF_MODE_TABLE
    };
    radio.setRfSwitchTable(pins,modes);
#else
    SPI.begin();
    if(radio.begin(869.525,125.0,9,5,0x12,14,8,1.8)!=RADIOLIB_ERR_NONE) return false;
    radio.setRfSwitchPins(5,RADIOLIB_NC);
#endif
#if !defined(ARDUINO_LILYGO_LORA_LR1121) && !defined(DEVICE_RELAY)
    if(radio.setDio2AsRfSwitch(true)!=RADIOLIB_ERR_NONE) return false;
#endif
    if(radio.setFrequency(869.525)!=RADIOLIB_ERR_NONE ||
       radio.setBandwidth(125.0)!=RADIOLIB_ERR_NONE ||
       radio.setSpreadingFactor(9)!=RADIOLIB_ERR_NONE ||
       radio.setCodingRate(5)!=RADIOLIB_ERR_NONE ||
       radio.setSyncWord(0x12)!=RADIOLIB_ERR_NONE ||
       radio.setOutputPower(14)!=RADIOLIB_ERR_NONE ||
       radio.setPreambleLength(8)!=RADIOLIB_ERR_NONE ||
       radio.setCRC(true)!=RADIOLIB_ERR_NONE) return false;
    radio.setPacketReceivedAction(onRadio);
    return radio.startReceive()==RADIOLIB_ERR_NONE;
}
int radioReceive(uint8_t* p,size_t cap) {
    if(transmitting || !interrupt) return 0;
    interrupt=false;
    size_t n=radio.getPacketLength();
    int result=-1;
    if(n>0 && n<=cap && radio.readData(p,n)==RADIOLIB_ERR_NONE) result=int(n);
    radio.startReceive(); return result;
}
bool radioBusy() {
    if(transmitting || interrupt) return true;
    // RadioLib's synchronous CAD waits indefinitely for its IRQ. Poll the IRQ status
    // over SPI instead of the DIO1 pin level, bounded so a missing result cannot trap
    // the user on the sending screen.
    int result=radio.startChannelScan();
    const uint32_t scanStart=millis();
    if(result==RADIOLIB_ERR_NONE) {
        do { delay(2); result=radio.getChannelScanResult(); }
        while(result==RADIOLIB_ERR_UNKNOWN && millis()-scanStart<250);
    }
    radio.standby(); interrupt=false;
    radio.startReceive();
    // Only detected LoRa activity counts as busy; a missing result or radio error is
    // not evidence of traffic and must not hold transmissions back.
    const bool busy=result==RADIOLIB_LORA_DETECTED;
    if(result!=RADIOLIB_CHANNEL_FREE) {
        const char* reason=busy ? "LoRa activity" : result==RADIOLIB_ERR_UNKNOWN ? "no scan result" : "radio error";
        Serial.printf("[radio] channel check: %s (%d) after %lu ms%s\n",reason,result,
            (unsigned long)(millis()-scanStart),busy ? "" : ", treating as free");
    }
    return busy;
}
bool radioSend(const uint8_t* p,size_t n) {
    radio.standby(); interrupt=false;
    radio.setPacketSentAction(onRadio);
    int result=radio.startTransmit(p,n);
    if(result!=RADIOLIB_ERR_NONE) {
        radio.setPacketReceivedAction(onRadio); radio.startReceive(); return false;
    }
    transmitting=true; started=millis(); return true;
}
int radioResult() {
    if(!transmitting) return 0;
    if(!interrupt && millis()-started<5000) return 0;
    // Check the actual TX-done IRQ: a timeout must never appear as success.
#if defined(ARDUINO_LILYGO_LORA_LR1121) || defined(DEVICE_RELAY)
    bool completed=interrupt && (radio.getIrqFlags() & RADIOLIB_LR11X0_IRQ_TX_DONE);
#else
    bool completed=interrupt && (radio.getIrqFlags() & RADIOLIB_SX126X_IRQ_TX_DONE);
#endif
    interrupt=false;
    int finish=radio.finishTransmit(); transmitting=false;
    radio.setPacketReceivedAction(onRadio); radio.startReceive();
    return completed && finish==RADIOLIB_ERR_NONE ? 1 : -1;
}
uint32_t radioAirtime(size_t n) { return (radio.getTimeOnAir(n)+999)/1000; }

#ifdef DEVICE_RELAY
void relayRadioOff() {
    radio.standby();
    radio.clearPacketReceivedAction();
    radio.clearPacketSentAction();
    // Cold sleep disables the receiver and TCXO; a wake starts a fresh boot.
    if(radio.sleep(false,0)!=RADIOLIB_ERR_NONE) {
        pinMode(42,OUTPUT); digitalWrite(42,LOW); // failed radio held in reset
    }
    transmitting=false; interrupt=false;
    SPI.end();
}
#endif
