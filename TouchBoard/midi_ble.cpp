#include <Arduino.h>
#include <NimBLEDevice.h>
#include "midi_ble.h"
#include "hidkb.h"
#include "midi_core.h"   // midi_serialEnabled(): mute logs when USB-MIDI owns UART0
#include "config.h"

// Apple BLE-MIDI service + characteristic UUIDs (fixed by the spec).
static const char* MIDI_SERVICE_UUID = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
static const char* MIDI_CHAR_UUID    = "7772E5DB-3868-4112-A1A9-F2669D106BF3";

static NimBLECharacteristic* s_char       = nullptr;
static volatile bool         s_subscribed = false;

class MidiCharCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&, uint16_t subValue) override {
    s_subscribed = (subValue != 0);
    if (!midi_serialEnabled())
      Serial.printf("[midi-ble] host %s notifications\n",
                    s_subscribed ? "subscribed to" : "unsubscribed from");
  }
  // Host->device MIDI (e.g. a DAW driving playback) arrives here. We accept it
  // silently for now; a future MIDI-in monitor can parse the packet body,
  // which is [header][ (tsByte, midi-bytes)... ] per the BLE-MIDI spec.
  void onWrite(NimBLECharacteristic*, NimBLEConnInfo&) override {}
};

void midi_ble_begin() {
  NimBLEServer* server = hidkb_server();
  if (!server) { Serial.println("[midi-ble] no BLE server; MIDI-BLE disabled"); return; }

  NimBLEService* svc = server->createService(MIDI_SERVICE_UUID);
  s_char = svc->createCharacteristic(
      MIDI_CHAR_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE |
      NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY);
  s_char->setCallbacks(new MidiCharCallbacks());
  // No svc->start() here: it is a deprecated no-op in NimBLE 2.x. The service
  // registers when the server first starts, which happens on the first
  // hidkb_advertiseHID()/midi_ble_advertise() call after this returns.
  Serial.println("[midi-ble] MIDI GATT service created");
}

// Tie "subscribed" to a live connection so it can't stay stuck true after the
// host drops (CCCD state dies with the link but our flag wouldn't).
bool midi_ble_subscribed() { return s_subscribed && hidkb_connected(); }

// One MIDI message -> one BLE-MIDI packet: [header][timestampLow][status][d1](d2).
// Timestamp is 13 bits of millis(): header carries the high 6, the timestamp
// byte the low 7, each with bit7 set as the spec requires.
void midi_ble_send(const uint8_t* bytes, size_t len) {
  if (!s_char || !s_subscribed || !hidkb_connected() || len == 0 || len > 3) return;
  uint16_t ts = (uint16_t)(millis() & 0x1FFF);
  uint8_t pkt[5];
  pkt[0] = 0x80 | ((ts >> 7) & 0x3F);   // header + timestampHigh
  pkt[1] = 0x80 | (ts & 0x7F);          // timestampLow
  for (size_t i = 0; i < len; i++) pkt[2 + i] = bytes[i];
  s_char->setValue(pkt, 2 + len);
  s_char->notify();
}

// Advertise so a MIDI host can find us. The 128-bit service UUID fills the
// primary packet; the (longer) device name rides in the scan response.
void midi_ble_advertise() {
  // If BLE init failed, midi_ble_begin() never ran and touching any NimBLE API
  // here would assert deep in the stack and boot-loop the board. Bail safely.
  if (!s_char) { Serial.println("[midi-ble] BLE unavailable; cannot advertise"); return; }
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->stop();
  adv->clearData();
  adv->addServiceUUID(MIDI_SERVICE_UUID);
  NimBLEAdvertisementData scan;
  scan.setName(MIDI_BLE_NAME);
  adv->setScanResponseData(scan);
  adv->enableScanResponse(true);
  adv->start();
  Serial.printf("[midi-ble] advertising as MIDI '%s'\n", MIDI_BLE_NAME);
}
