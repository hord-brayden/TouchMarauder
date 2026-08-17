#pragma once
//
// Board: ESP32-2432S024C ("Cheap Yellow Display" 2.4", capacitive variant)
// MCU: ESP32-WROOM-32 (classic dual-mode BT, no PSRAM)
//
// All pin assignments below are for the CAPACITIVE (C) variant.
// If touch init logs "no CST8xx found", you likely have the resistive (R)
// variant (XPT2046 on SPI) and touch.cpp needs a different driver.

// ---------- Bluetooth ----------
#define BLE_DEVICE_NAME   "TouchBoard"
#define BLE_MANUFACTURER  "BHord"

// How long a tapped key is held down before release, in ms. Must exceed one
// BLE connection interval or the host misses the press. 40ms is safe on
// iOS/Android/desktop; lower it for snappier fast typing if your host allows.
#define HID_TAP_HOLD_MS   40

// ---------- Display (ILI9341, SPI2/HSPI) ----------
#define PIN_LCD_SCK   14
#define PIN_LCD_MOSI  13
#define PIN_LCD_MISO  12
#define PIN_LCD_CS    15
#define PIN_LCD_DC    2
#define PIN_LCD_RST   -1   // tied to EN
#define PIN_LCD_BL    27   // backlight, PWM

#define SCREEN_W      240  // setRotation(0) -> portrait, the T9 orientation
#define SCREEN_H      320

// ---------- Capacitive touch (CST820, I2C) ----------
#define PIN_TOUCH_SDA 33
#define PIN_TOUCH_SCL 32
#define PIN_TOUCH_RST 25
#define PIN_TOUCH_INT 21   // unused; we poll over I2C

// Raw touch is portrait 240x320, same as the screen now — no swap, but the
// panel origin is opposite the display origin (derived from the verified
// landscape mapping). If touches mirror, flip these and reflash.
#define TOUCH_SWAP_XY 0
#define TOUCH_INV_X   0
#define TOUCH_INV_Y   0

// Only trust touch data accompanied by a recent INT pulse from the chip;
// stale/garbage register reads without one are discarded.
#define TOUCH_INT_GATE_MS 150

// ---------- T9 typing (traditional buffered multi-tap) ----------
#define T9_MULTITAP_MS  1100  // window to tap the same key again and cycle
#define T9_LONGPRESS_MS 600   // hold a key this long to type its digit
#define T9_MIN_TAP_MS   35    // shorter contacts are ghost blips, ignored

// ---------- Onboard RGB LED (active LOW) ----------
#define PIN_LED_R     4
#define PIN_LED_G     16
#define PIN_LED_B     17

// ---------- Speaker (metronome click / key feedback) ----------
// The 2432S024 routes a small buzzer/amp to GPIO 26. tone() drives it.
#define PIN_SPEAKER   26

// ---------- SD card (VSPI, separate bus from the LCD's HSPI) ----------
// Standard CYD wiring. If SD_MOUNTED stays false at boot, re-check these
// against your board — some CYD revisions move CS. No LCD pin overlap.
#define PIN_SD_SCK    18
#define PIN_SD_MISO   19
#define PIN_SD_MOSI   23
#define PIN_SD_CS      5
#define SD_DIR_SONGS  "/midi"   // .mid files live here

// ---------- MIDI ----------
#define MIDI_BLE_NAME       "TouchBoard MIDI"   // shown in macOS Audio MIDI Setup > Bluetooth
#define MIDI_SERIAL_BAUD    115200              // Hairless / bridge script baud on UART0
#define MIDI_DEFAULT_CH     0                   // 0-based channel (ch 1)
#define MIDI_DEFAULT_VEL    100                 // velocity when not slider-driven
#define MIDI_DEFAULT_OCTAVE 4                   // middle-C octave shown first
#define MIDI_DEFAULT_BPM    120
#define MIDI_MAX_EVENTS     4000                // song buffer; ~32KB of RAM
#define MIDI_SMF_PPQ        480                 // ticks per quarter note in saved files

// NVS hand-off: Marauder (or TouchBoard itself) can request that the board
// boot straight into the MIDI view. Namespace is kept distinct from the
// Marauder bond namespace (mrdr_bond) so nothing collides.
#define NVS_TB_NAMESPACE    "touchboard"
#define NVS_KEY_BOOTVIEW    "bootview"          // value 1 == open MIDI on boot
