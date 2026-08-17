//
// TouchBoard — BLE HID keyboard on an ESP32-2432S024C touchscreen.
//
// Views (tab bar at top): QWERTY keyboard / numpad / nav+arrows / Bluetooth.
// Pairs as a real hardware keyboard with any BLE HID host (phone, laptop, TV).
//
// Build: Arduino IDE, board "ESP32 Dev Module", Partition Scheme "Huge APP".
// Libraries: NimBLE-Arduino 2.x, LovyanGFX 1.2.x. See README.md.
//
#include "config.h"
#include "display.h"
#include "touch.h"
#include "hidkb.h"
#include "ui.h"
#include "midi_core.h"
#include "midi_ble.h"
#include "midi_serial.h"
#include "midi_ui.h"
#include "song.h"
#include "sdstore.h"
#include <Preferences.h>

LGFX tft;

static bool bleOk = false;

// Onboard RGB LED (active LOW): blue blink = advertising, green = connected,
// solid red = BLE stack failed to start (check serial).
static void ledUpdate(uint32_t now) {
  if (!bleOk) {
    digitalWrite(PIN_LED_R, LOW);
    digitalWrite(PIN_LED_G, HIGH);
    digitalWrite(PIN_LED_B, HIGH);
    return;
  }
  bool conn = hidkb_connected();
  bool blink = (now / 500) % 2;
  digitalWrite(PIN_LED_R, HIGH);
  digitalWrite(PIN_LED_G, conn ? LOW : HIGH);
  digitalWrite(PIN_LED_B, (!conn && blink) ? LOW : HIGH);
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  digitalWrite(PIN_LED_R, HIGH);
  digitalWrite(PIN_LED_G, HIGH);
  digitalWrite(PIN_LED_B, HIGH);

  tft.init();
  tft.setRotation(2);        // portrait, 240x320 — USB-C down, matches Marauder
  tft.setBrightness(200);

  touch_begin();
  bleOk = hidkb_begin();   // UI still runs without BLE so you can debug
  if (bleOk) midi_ble_begin();   // create MIDI GATT service BEFORE first advertise

  sd_begin();              // mount the SD card for songs (fine if none present)
  midi_serial_begin();     // USB-serial MIDI backend (idle until enabled in SET)
  midiui_begin();          // MIDI engine + sequencer

  // First advertise = first GATT server start = HID + MIDI both register. Do it
  // here, after both services exist. If we boot into MIDI, ui_begin() below
  // switches the advertising payload to the MIDI profile.
  if (bleOk) hidkb_advertiseHID();

  // Dual-boot hand-off: Marauder's "MIDI" tile leaves a one-shot NVS flag so we
  // open straight into the MIDI view. Consume it so a normal reboot is normal.
  {
    Preferences prefs;
    if (prefs.begin(NVS_TB_NAMESPACE, false)) {
      if (prefs.getUChar(NVS_KEY_BOOTVIEW, 0) == 1) {
        prefs.putUChar(NVS_KEY_BOOTVIEW, 0);
        ui_bootIntoMidi();
        Serial.println("[main] NVS hand-off: opening MIDI view");
      }
      prefs.end();
    }
  }

  ui_begin();

  Serial.println("[main] ready");
}

void loop() {
  static bool wasDown = false;
  uint32_t now = millis();

  TouchPoint p;
  bool isDown = touch_read(p);
  if (isDown && !wasDown) {
    // Calibration aid — muted while USB-serial MIDI is active so debug text
    // can't corrupt the raw MIDI byte stream sharing UART0.
    if (!midi_serialEnabled()) Serial.printf("[touch] down at %d,%d\n", p.x, p.y);
    ui_onTouchDown(p.x, p.y);
  } else if (!isDown && wasDown) {
    ui_onTouchUp();
  } else if (isDown && wasDown) {
    ui_onTouchMove(p.x, p.y);   // drag: MIDI faders / pitch bend / glissando
  }
  wasDown = isDown;

  ui_tick(now);
  song_tick(now);   // sequencer scheduler runs regardless of the active view
  ledUpdate(now);
  delay(8);  // ~120 Hz touch poll; plenty for tapping, easy on the I2C bus
}
