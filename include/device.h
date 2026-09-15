#pragma once
#include <stddef.h>
#include <stdint.h>
// Modifier: a keyboard modifier changed (no action, but it counts as activity).
enum class Key { None, Character, Up, Down, Left, Right, Select, Back, Send, Quick, Modifier };
struct Input { Key key=Key::None; char character=0; };
// Eight rows of 21 characters on Wio; eight rows of 40 characters on Pager.
// Row 0 is the status bar.
struct Screen { char lines[8][41]={}; uint8_t highlight=255, caretRow=255, caretColumn=0; };
bool deviceBegin();
void deviceTick();
Input deviceInput();
void deviceDraw(const Screen& screen);
// Turn the display and its backlight off or on; the caller redraws after waking.
void deviceDisplay(bool on);
int deviceColumns();
// Battery charge 0-100, or -1 when unknown.
int deviceBattery();
// Short notification sound for a received message.
void deviceChime();
// Active keyboard modifier label, "" when none. Returns string literals.
const char* deviceModifier();
uint64_t deviceId();
uint32_t deviceRandom();
bool storageBegin();
size_t storageRead(int slot,uint8_t* data,size_t capacity);
bool storageWrite(int slot,const uint8_t* data,size_t size);
bool radioBegin();
int radioReceive(uint8_t* data,size_t capacity);
bool radioBusy();
bool radioSend(const uint8_t* data,size_t size);
// 0 pending, 1 sent, -1 failed. One result per transmission.
int radioResult();
uint32_t radioAirtime(size_t size);
