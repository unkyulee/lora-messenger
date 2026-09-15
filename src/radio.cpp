#include <Arduino.h>
#include <RadioLib.h>
#include "device.h"
#include "chat.h"
#ifdef DEVICE_PAGER
#include <LilyGoLib.h>
#else
#include <SPI.h>
static SX1262 radio=new Module(4,1,2,3);
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
#else
    SPI.begin();
    if(radio.begin(869.525,125.0,9,5,0x12,14,8,1.8)!=RADIOLIB_ERR_NONE) return false;
    radio.setRfSwitchPins(5,RADIOLIB_NC);
#endif
#ifndef ARDUINO_LILYGO_LORA_LR1121
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
    // RadioLib's synchronous CAD waits indefinitely for its IRQ. Bound this
    // wait so a missing interrupt cannot trap the user on the sending screen.
    int result=radio.startChannelScan();
    const uint32_t scanStart=millis();
#ifdef DEVICE_PAGER
    constexpr int irqPin=LORA_IRQ;
#else
    constexpr int irqPin=1;
#endif
    if(result==RADIOLIB_ERR_NONE) {
        while(!digitalRead(irqPin) && millis()-scanStart<250) delay(1);
        result=digitalRead(irqPin) ? radio.getChannelScanResult() : RADIOLIB_ERR_RX_TIMEOUT;
    }
    radio.standby(); interrupt=false;
    radio.startReceive();
    return result!=RADIOLIB_CHANNEL_FREE;
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
#ifdef ARDUINO_LILYGO_LORA_LR1121
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
