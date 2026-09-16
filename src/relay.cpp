#ifdef DEVICE_RELAY
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include "device.h"
#include "network.h"

namespace {
network::Relay relay;
chat::AirtimeGate gate;
uint8_t packet[network::PacketMax];
bool ready=false, sending=false;
uint32_t reportAt=0, forwarded=0;
}
void setup() {
    Serial.begin(115200);
    randomSeed(NRF_FICR->DEVICEID[0]^NRF_FICR->DEVICEID[1]^micros());
    ready=radioBegin(); gate.start(millis());
}
void loop() {
    const uint32_t now=millis();
    if(chat::due(now,reportAt)) {
        reportAt=now+10000;
        Serial.printf("[relay] radio=%s forwarded=%lu\n",ready?"ready":"FAILED",(unsigned long)forwarded);
    }
    // A short heartbeat; fast blinking reports radio startup failure.
    digitalWrite(LED_BUILTIN,now%(ready?5000:500)<50);
    if(!ready) { delay(10); return; }
    if(sending && radioResult()) sending=false;
    int n=radioReceive(packet,sizeof(packet));
    network::Frame f;
    if(n>0 && network::decode(packet,size_t(n),f)) relay.accept(f,now,uint32_t(random(0x7fffffff)));
    if(!sending) if(auto* q=relay.next(now)) {
        size_t length=network::encode(q->frame,packet,sizeof(packet));
        const uint32_t air=radioAirtime(length);
        if(length && gate.ready(now,air)) {
            if(radioBusy()) q->due=millis()+200+random(1000);
            else {
                gate.charge(millis(),air); q->used=false;
                sending=radioSend(packet,length);
                if(sending) ++forwarded;
            }
        }
    }
    delay(5);
}
#endif
