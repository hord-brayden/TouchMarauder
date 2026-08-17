#pragma once
#include <Arduino.h>

class NimBLEServer;   // opaque; midi_ble adds its service to this same server

// HID modifier bits (byte 0 of the keyboard report)
#define KMOD_CTRL  0x01
#define KMOD_SHIFT 0x02
#define KMOD_ALT   0x04
#define KMOD_GUI   0x08

bool   hidkb_begin();   // false if the BLE stack failed to start

// The one NimBLE server for the whole device. BLE-MIDI hangs its GATT service
// off this rather than re-initialising the stack (re-init boot-loops the ESP32).
NimBLEServer* hidkb_server();

// Rebuild the advertising payload as a HID keyboard and (re)start advertising.
// Called on boot and whenever we return to keyboard mode from MIDI mode.
void hidkb_advertiseHID();
bool   hidkb_connected();
String hidkb_peerAddress();   // last/current host, "" if none
int    hidkb_bondCount();     // how many hosts have pairing keys stored

// Real keyboards send key-DOWN when you press and key-UP when you release;
// the HOST does auto-repeat. We mirror that: press on touch-down,
// releaseAll on touch-up. Hold backspace -> it repeats, for free.
void hidkb_press(uint8_t usage, uint8_t mods);
void hidkb_releaseAll();

// Immediate press+release, for T9 multi-tap where the "key" is logical.
void hidkb_tap(uint8_t usage, uint8_t mods);
// ASCII -> HID usage + modifiers (e.g. '?' = Shift + '/' key).
bool hidkb_charToUsage(char c, uint8_t& usage, uint8_t& mods);
void hidkb_tapChar(char c);

void hidkb_restartAdvertising();
void hidkb_clearBonds();      // forget all paired hosts + disconnect
