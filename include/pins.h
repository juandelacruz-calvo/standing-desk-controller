#ifndef PINS_H
#define PINS_H

#include <Arduino.h>

#ifdef ESP8266
// --- Wemos D1 Mini (ESP8266) pin assignments ---
// We use D3/D4 for motors because they are pulled HIGH at boot, 
// preventing active-low motor triggers during ESP8266 startup.
constexpr uint8_t PIN_MOTOR_UP  = D3;   // GPIO0
constexpr uint8_t PIN_MOTOR_DN  = D4;   // GPIO2

constexpr uint8_t PIN_TRIG      = D8;   // GPIO15 (LOW at boot, safe for TRIG)
constexpr uint8_t PIN_ECHO      = D6;   // GPIO12

constexpr uint8_t PIN_BTN_UP    = D2;   // GPIO4
constexpr uint8_t PIN_BTN_DN    = D7;   // GPIO13
constexpr uint8_t PIN_BTN_MEM1  = D1;   // GPIO5
constexpr uint8_t PIN_BTN_MEM2  = D5;   // GPIO14
constexpr uint8_t PIN_BTN_MEM3  = D0;   // GPIO16

#elif defined(ARDUINO_AVR_NANO)
/**
 * --- Arduino Nano "Plug and Play" Mapping ---
 * This mapping allows you to plug both Wemos D1 Mini DuPont headers into the 
 * same digital side of the Nano (GND through D11) in a single continuous block.
 * 
 * 1. Header 2 (5-pin: GND, D4, D3, D2, D1):
 *    Plug into Nano pins [GND, D2, D3, D4, D5].
 *    (Align header GND with Nano GND)
 * 
 * 2. Header 1 (6-pin: 3V3, D8, D7, D6, D5, D0):
 *    Plug into Nano pins [D6, D7, D8, D9, D10, D11].
 *    (3V3 wire is empty/not plugged, aligns with Nano D6)
 */

constexpr uint8_t PIN_MOTOR_DN  = 2;    // D1 Mini D4
constexpr uint8_t PIN_MOTOR_UP  = 3;    // D1 Mini D3
constexpr uint8_t PIN_BTN_UP    = 4;    // D1 Mini D2
constexpr uint8_t PIN_BTN_MEM1  = 5;    // D1 Mini D1

constexpr uint8_t PIN_TRIG      = 7;    // D1 Mini D8
constexpr uint8_t PIN_BTN_DN    = 8;    // D1 Mini D7
constexpr uint8_t PIN_ECHO      = 9;    // D1 Mini D6
constexpr uint8_t PIN_BTN_MEM2  = 10;   // D1 Mini D5
constexpr uint8_t PIN_BTN_MEM3  = 11;   // D1 Mini D0

#else
#error "Unsupported board! Please define pins for your hardware in include/pins.h"
#endif

#endif // PINS_H
