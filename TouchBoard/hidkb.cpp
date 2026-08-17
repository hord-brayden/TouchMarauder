//
// BLE HID keyboard built directly on NimBLE-Arduino 2.x.
//
// Why not the popular ESP32-BLE-Keyboard library? It targets the old
// Bluedroid stack and breaks on esp32 core 3.x (which you're running).
// NimBLE is lighter on RAM and its NimBLEHIDDevice helper does the
// HID-over-GATT plumbing; we only supply the report map and the reports.
//
#include "hidkb.h"
#include "config.h"
#include "midi_core.h"    // midi_serialEnabled(): mute logs when USB-MIDI owns UART0
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

// Standard 8-byte keyboard input report: [modifiers][reserved][6 keycodes].
// Also declares the host->device LED output report (caps lock etc.) because
// many hosts expect a keyboard to have one, even if we ignore it.
static const uint8_t REPORT_MAP[] = {
  0x05, 0x01,        // Usage Page (Generic Desktop)
  0x09, 0x06,        // Usage (Keyboard)
  0xA1, 0x01,        // Collection (Application)
  0x85, 0x01,        //   Report ID (1)
  // -- input: modifier bits --
  0x05, 0x07,        //   Usage Page (Keyboard/Keypad)
  0x19, 0xE0,        //   Usage Min (LeftControl)
  0x29, 0xE7,        //   Usage Max (Right GUI)
  0x15, 0x00,        //   Logical Min (0)
  0x25, 0x01,        //   Logical Max (1)
  0x75, 0x01,        //   Report Size (1)
  0x95, 0x08,        //   Report Count (8)
  0x81, 0x02,        //   Input (Data, Var, Abs)
  // -- input: reserved byte --
  0x95, 0x01,        //   Report Count (1)
  0x75, 0x08,        //   Report Size (8)
  0x81, 0x01,        //   Input (Const)
  // -- output: LED states (caps/num/scroll) --
  0x95, 0x05,        //   Report Count (5)
  0x75, 0x01,        //   Report Size (1)
  0x05, 0x08,        //   Usage Page (LEDs)
  0x19, 0x01,        //   Usage Min (Num Lock)
  0x29, 0x05,        //   Usage Max (Kana)
  0x91, 0x02,        //   Output (Data, Var, Abs)
  0x95, 0x01,        //   Report Count (1)
  0x75, 0x03,        //   Report Size (3)
  0x91, 0x01,        //   Output (Const) — padding
  // -- input: up to 6 simultaneous keycodes --
  0x95, 0x06,        //   Report Count (6)
  0x75, 0x08,        //   Report Size (8)
  0x15, 0x00,        //   Logical Min (0)
  0x25, 0x65,        //   Logical Max (101)
  0x05, 0x07,        //   Usage Page (Keyboard/Keypad)
  0x19, 0x00,        //   Usage Min (0)
  0x29, 0x65,        //   Usage Max (101)
  0x81, 0x00,        //   Input (Data, Array)
  0xC0               // End Collection
};

static NimBLEServer*         g_server = nullptr;
static NimBLEHIDDevice*      g_hid    = nullptr;
static NimBLECharacteristic* g_input  = nullptr;
static volatile bool         g_connected = false;
static uint16_t              g_connHandle = 0;
static String                g_peerAddr = "";

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
    g_connected  = true;
    g_connHandle = connInfo.getConnHandle();
    g_peerAddr   = connInfo.getAddress().toString().c_str();
    if (!midi_serialEnabled()) Serial.printf("[ble] connected: %s\n", g_peerAddr.c_str());
  }
  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
    g_connected = false;
    if (!midi_serialEnabled()) Serial.printf("[ble] disconnected (reason %d), advertising again\n", reason);
    // advertiseOnDisconnect(true) restarts advertising for us
  }
  void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
    if (!midi_serialEnabled())
      Serial.printf("[ble] auth complete, encrypted=%d bonded=%d\n",
                    connInfo.isEncrypted(), connInfo.isBonded());
  }
};

bool hidkb_begin() {
  // init() can fail (controller/NVS problems) and returns false. Calling any
  // NimBLE API after a failed init trips an assert deep in the stack and
  // boot-loops the board, so bail out here instead.
  if (!NimBLEDevice::init(BLE_DEVICE_NAME)) {
    Serial.println("[ble] FATAL: NimBLEDevice::init() failed - BLE disabled");
    return false;
  }

  // HID over BLE requires an encrypted, bonded link (iOS/Android enforce it).
  // No-input-no-output IO caps -> "Just Works" pairing, no PIN prompt.
  NimBLEDevice::setSecurityAuth(true /*bond*/, false /*mitm*/, true /*secure conn*/);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  g_server = NimBLEDevice::createServer();
  g_server->setCallbacks(new ServerCallbacks(), true);
  g_server->advertiseOnDisconnect(true);

  g_hid   = new NimBLEHIDDevice(g_server);
  g_input = g_hid->getInputReport(1);   // report ID 1
  g_hid->getOutputReport(1);            // LED report; created so hosts can write it
  g_hid->setManufacturer(BLE_MANUFACTURER);
  g_hid->setPnp(0x02, 0x303A, 0x0001, 0x0100);  // USB-IF VID source, Espressif VID
  g_hid->setHidInfo(0x00, 0x01);
  g_hid->setReportMap((uint8_t*)REPORT_MAP, sizeof(REPORT_MAP));
  g_hid->startServices();
  g_hid->setBatteryLevel(100);

  // NOTE: advertising is deliberately NOT started here. The FIRST call to
  // NimBLEAdvertising::start() is what starts the GATT server and registers
  // every service, so midi_ble_begin() must create the MIDI service before
  // that happens. setup() calls hidkb_advertiseHID() after midi_ble_begin().
  return true;
}

NimBLEServer* hidkb_server() { return g_server; }

// Build a HID-keyboard advertising payload and start it. clearData() wipes any
// prior payload (e.g. the MIDI service UUID) so switching modes leaves nothing
// stale. The 16-bit HID UUID + appearance + short name all fit the 31-byte
// primary packet, so no scan response is needed here.
void hidkb_advertiseHID() {
  if (!g_hid) return;
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->stop();
  adv->clearData();
  adv->enableScanResponse(false);
  adv->setAppearance(961);  // 0x03C1 = HID Keyboard; hosts show a keyboard icon
  adv->addServiceUUID(g_hid->getHidService()->getUUID());
  adv->setName(BLE_DEVICE_NAME);
  adv->start();
  Serial.printf("[ble] advertising as HID '%s'\n", BLE_DEVICE_NAME);
}

bool   hidkb_connected()   { return g_connected; }
String hidkb_peerAddress() { return g_connected ? g_peerAddr : String(""); }
int    hidkb_bondCount()   { return NimBLEDevice::getNumBonds(); }

void hidkb_press(uint8_t usage, uint8_t mods) {
  if (!g_connected || !g_input) return;
  uint8_t report[8] = { mods, 0, usage, 0, 0, 0, 0, 0 };
  g_input->setValue(report, sizeof(report));
  g_input->notify();
}

void hidkb_releaseAll() {
  if (!g_connected || !g_input) return;
  uint8_t report[8] = { 0 };
  g_input->setValue(report, sizeof(report));
  g_input->notify();
}

void hidkb_tap(uint8_t usage, uint8_t mods) {
  // A real key is held for tens of ms; the host needs the key-down and key-up
  // notifications to land in SEPARATE BLE connection events or it sees no
  // press. Sending them back-to-back (what this did before) made T9-committed
  // characters vanish even though the reports were correct. Hold long enough
  // to clear one connection interval on any host (iOS negotiates up to ~30ms).
  hidkb_press(usage, mods);
  delay(HID_TAP_HOLD_MS);
  hidkb_releaseAll();
}

bool hidkb_charToUsage(char c, uint8_t& usage, uint8_t& mods) {
  mods = 0;
  if (c >= 'a' && c <= 'z') { usage = 0x04 + (c - 'a'); return true; }
  if (c >= 'A' && c <= 'Z') { usage = 0x04 + (c - 'A'); mods = KMOD_SHIFT; return true; }
  if (c >= '1' && c <= '9') { usage = 0x1E + (c - '1'); return true; }
  switch (c) {
    case '0':  usage = 0x27; return true;
    case ' ':  usage = 0x2C; return true;
    case '.':  usage = 0x37; return true;
    case ',':  usage = 0x36; return true;
    case '\'': usage = 0x34; return true;
    case '"':  usage = 0x34; mods = KMOD_SHIFT; return true;
    case '?':  usage = 0x38; mods = KMOD_SHIFT; return true;
    case '!':  usage = 0x1E; mods = KMOD_SHIFT; return true;
    case '@':  usage = 0x1F; mods = KMOD_SHIFT; return true;
    case '#':  usage = 0x20; mods = KMOD_SHIFT; return true;
    case '-':  usage = 0x2D; return true;
    case '_':  usage = 0x2D; mods = KMOD_SHIFT; return true;
    case '/':  usage = 0x38; return true;
    case ':':  usage = 0x33; mods = KMOD_SHIFT; return true;
    case ';':  usage = 0x33; return true;
  }
  return false;
}

void hidkb_tapChar(char c) {
  uint8_t usage, mods;
  if (hidkb_charToUsage(c, usage, mods)) hidkb_tap(usage, mods);
}

void hidkb_restartAdvertising() {
  NimBLEDevice::stopAdvertising();
  NimBLEDevice::startAdvertising();
  Serial.println("[ble] advertising restarted");
}

void hidkb_clearBonds() {
  NimBLEDevice::deleteAllBonds();
  if (g_connected && g_server) {
    // The host still holds its copy of the keys; dropping the link forces a
    // fresh pairing attempt (you must also "Forget" the device on the host).
    g_server->disconnect(g_connHandle);
  }
  Serial.println("[ble] all bonds cleared");
}
