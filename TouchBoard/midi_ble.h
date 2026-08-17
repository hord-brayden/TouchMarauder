#pragma once
#include <stdint.h>
#include <stddef.h>

// BLE-MIDI backend (Apple "MIDI over Bluetooth LE" spec). It adds its GATT
// service to the NimBLE server that hidkb already created, so the stack is
// initialised exactly once. macOS/iOS discover it in Audio MIDI Setup >
// Bluetooth; Windows/Android need a BLE-MIDI bridge app.

void midi_ble_begin();                                 // create the MIDI GATT service
void midi_ble_send(const uint8_t* bytes, size_t len);  // wrap + notify one message
void midi_ble_advertise();                             // advertise as a MIDI peripheral
bool midi_ble_subscribed();                            // a host is listening for notifies
