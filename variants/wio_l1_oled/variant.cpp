#include "variant.h"
#include "wiring_constants.h"
#include "wiring_digital.h"
// Arduino index -> physical nRF GPIO number (P1.x = 32+x).
// Verified against the Wio L1 hardware mapping; unused QSPI remains idle.
extern "C" {
const uint32_t g_ADigitalPinMap[PINS_COUNT] = {
    41,7,39,42,46,40,27,26,30,3,28,33,32,8,6,5,31,43,44,
    21,25,20,24,22,23,36,12,11,35,37,4
};
}
void initVariant() {
    pinMode(20,OUTPUT); digitalWrite(20,HIGH); // QSPI CS inactive
    pinMode(30,OUTPUT); digitalWrite(30,LOW);  // battery divider idle
    pinMode(11,OUTPUT); digitalWrite(11,LOW);
    pinMode(12,OUTPUT); digitalWrite(12,LOW);  // buzzer idle
}
