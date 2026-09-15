#pragma once
#include <stdint.h>
extern uint32_t fakeTime;
inline uint32_t millis() { return fakeTime; }
inline void delay(uint32_t n) { fakeTime+=n; }
struct FakeSerial { void println(const char*) {} };
extern FakeSerial Serial;
