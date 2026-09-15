#pragma once
#include "WVariant.h"
#define VARIANT_MCK 64000000ul
#define USE_LFXO
#define PINS_COUNT 31
#define NUM_DIGITAL_PINS 31
#define NUM_ANALOG_INPUTS 1
#define NUM_ANALOG_OUTPUTS 0
#define PIN_LED1 11
#define PIN_LED2 11
#define LED_BUILTIN 11
#define LED_CONN 11
#define LED_STATE_ON 1
#define PIN_BUTTON1 13
#define PIN_SERIAL1_RX 7
#define PIN_SERIAL1_TX 6
#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA 14
#define PIN_WIRE_SCL 15
#define SPI_INTERFACES_COUNT 1
#define PIN_SPI_MISO 9
#define PIN_SPI_MOSI 10
#define PIN_SPI_SCK 8
#define PIN_SPI_SS 4
#define PIN_A0 16
#define ADC_RESOLUTION 12
static const uint8_t SDA=PIN_WIRE_SDA, SCL=PIN_WIRE_SCL;
static const uint8_t SS=PIN_SPI_SS, MOSI=PIN_SPI_MOSI, MISO=PIN_SPI_MISO, SCK=PIN_SPI_SCK;
