#include "variant.h"
#include "wiring_constants.h"
#include "wiring_digital.h"
extern "C" {
const uint32_t g_ADigitalPinMap[PINS_COUNT]={
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47};
}
void initVariant() {
    // GPS, sensors, accelerometer and buzzer are unused by the relay.
    const uint8_t off[]={4,38,39,37,25,43,8,44,15,24};
    for(auto pin:off) { pinMode(pin,OUTPUT); digitalWrite(pin,LOW); }
    pinMode(47,OUTPUT); digitalWrite(47,HIGH); // GNSS held in reset
    pinMode(46,INPUT_PULLUP);
}
