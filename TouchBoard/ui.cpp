//
// Screen rendering, hit testing, and the view state machine. Portrait 240x320.
//
// T9 is traditional buffered multi-tap (the report in the project docs):
// taps edit a LOCAL candidate buffer shown in the preview strip — nothing
// goes over BLE until the character COMMITS, via:
//   - the multi-tap timeout expiring,
//   - a different key starting a new sequence,
//   - the '>' advance key (for same-key sequences like "ab"),
//   - Enter / leaving the view.
// One HID character per commit: no backspace churn, no dropped notifications.
//
// Ghost-touch defense: keypad keys fire on RELEASE, and only if the contact
// lasted >= T9_MIN_TAP_MS. A one-frame phantom never types anything.
//
#include <Arduino.h>
#include <ctype.h>
#include "ui.h"
#include "config.h"
#include "display.h"
#include "hidkb.h"
#include "keymap.h"
#include "midi_ui.h"         // MIDI controller mode (VIEW_MIDI)
#include "esp_ota_ops.h"     // dual-boot: hand off to the Marauder app slot
#include "esp_partition.h"

// ---------------- layout constants ----------------
static const int TAB_H   = 28;
static const int TAB_W   = SCREEN_W / VIEW_COUNT;   // 60
static const int STRIP_Y = TAB_H;                   // preview/status strip
static const int STRIP_H = 36;
static const int AREA_Y  = TAB_H + STRIP_H;         // 64
static const int AREA_H  = SCREEN_H - AREA_Y;       // 256
static const int KEY_GAP = 2;

// T9 grid: 3 cols x 5 rows (1-9, *0#, then bksp / advance / enter)
static const int T9_COL_W = SCREEN_W / 3;           // 80
static const int T9_ROW_H = AREA_H / 5;             // 51
static const int T9_BKSP    = 12;
static const int T9_ADVANCE = 13;
static const int T9_ENTER   = 14;

// ---------------- palette (RGB565) ----------------
static const uint16_t COL_BG       = 0x10A2;
static const uint16_t COL_KEY      = 0x39E7;
static const uint16_t COL_KEY_SPEC = 0x29A6;
static const uint16_t COL_KEY_DOWN = 0x04F3;
static const uint16_t COL_TEXT     = 0xFFFF;
static const uint16_t COL_TAB      = 0x2104;
static const uint16_t COL_TAB_ON   = 0x04F3;
static const uint16_t COL_OK       = 0x07E8;
static const uint16_t COL_ADV      = 0x04DF;
static const uint16_t COL_DIM      = 0x8C71;

// ---------------- state ----------------
static ViewId curView = VIEW_KB;

enum CaseMode : uint8_t { CASE_LOWER, CASE_SHIFT, CASE_CAPS };
static CaseMode caseMode = CASE_LOWER;

// T9 candidate buffer
static int      pendKey      = -1;   // key the open sequence belongs to
static int      pendIdx      = 0;
static char     pendChar     = 0;    // cased candidate; 0 = buffer empty
static uint32_t pendDeadline = 0;

// current physical touch on the T9 view
static int      t9DownCell = -1;
static uint32_t t9DownTime = 0;
static bool     t9LongDone = false;

// generic views (numpad/nav) pressed key
struct ActiveKey {
  const KeyDef* k;
  int x, y, w, h;
  int tab;
  bool valid;
};
static ActiveKey down = { nullptr, 0, 0, 0, 0, -1, false };

static bool lastConn  = false;
static int  lastBonds = -1;

static const char* TAB_LABELS[VIEW_COUNT] = { "T9", "123", "nav", "QWE", "BT", "MIDI" };

// True while a finger that landed in the MIDI content area is still down, so
// move/up events route to the MIDI module rather than the keyboard grids.
static bool midiTouchActive = false;
static bool s_bootMidi = false;

// BT screen buttons (stacked, portrait)
static const KeyDef BT_BTN_ADV   = { "Re-advertise",     0, 0, ACT_BT_ADV,        1 };
static const KeyDef BT_BTN_CLEAR = { "Forget hosts",     0, 0, ACT_BT_CLEAR,      1 };
static const KeyDef BT_BTN_EXIT  = { "Exit to Marauder", 0, 0, ACT_EXIT_MARAUDER, 1 };
static const int BT_BTN_X = 20, BT_BTN_W = 200, BT_BTN_H = 40;
static const int BT_BTN1_Y = SCREEN_H - 150;  // Re-advertise
static const int BT_BTN2_Y = SCREEN_H - 104;  // Forget hosts
static const int BT_BTN3_Y = SCREEN_H - 58;   // Exit to Marauder (dual-boot)

static const ViewDef* currentViewDef() {
  switch (curView) {
    case VIEW_NUM: return &VIEW_NUM_DEF;
    case VIEW_NAV: return &VIEW_NAV_DEF;
    default:       return nullptr;  // VIEW_KB and VIEW_BT draw themselves
  }
}

// ---------------- glyph drawing ----------------
static void drawGlyph(int cx, int cy, char code, uint16_t color) {
  const int s = 7;
  switch (code) {
    case 1:  tft.fillTriangle(cx, cy - s, cx - s, cy + s, cx + s, cy + s, color); break;
    case 2:  tft.fillTriangle(cx, cy + s, cx - s, cy - s, cx + s, cy - s, color); break;
    case 3:  tft.fillTriangle(cx - s, cy, cx + s, cy - s, cx + s, cy + s, color); break;
    case 4:  tft.fillTriangle(cx + s, cy, cx - s, cy - s, cx - s, cy + s, color); break;
    case 6:  // backspace: left arrow with tail
      tft.fillTriangle(cx - s - 2, cy, cx - 1, cy - 6, cx - 1, cy + 6, color);
      tft.fillRect(cx - 1, cy - 2, s + 3, 5, color);
      break;
    case 7:  // enter: down-then-left return arrow
      tft.fillRect(cx + 4, cy - s, 3, s + 2, color);
      tft.fillRect(cx - 3, cy - 1, 10, 3, color);
      tft.fillTriangle(cx - s, cy, cx - 1, cy - 5, cx - 1, cy + 5, color);
      break;
  }
}

// ---------------- preview / status strip ----------------
static void drawStrip() {
  tft.fillRect(0, STRIP_Y, SCREEN_W, STRIP_H, COL_TAB);
  bool conn = hidkb_connected();
  tft.fillCircle(14, STRIP_Y + STRIP_H / 2, 5, conn ? COL_OK : COL_ADV);

  tft.setTextDatum(textdatum_t::middle_left);
  tft.setFont(&fonts::FreeSans9pt7b);

  if (curView == VIEW_KB) {
    // the "Nokia screen": pending candidate + case mode
    if (pendChar) {
      char buf[4] = { pendChar == ' ' ? '_' : pendChar, 0 };
      tft.setTextDatum(textdatum_t::middle_center);
      tft.setFont(&fonts::FreeSansBold12pt7b);
      tft.setTextColor(COL_TAB_ON, COL_TAB);
      tft.drawString(buf, SCREEN_W / 2, STRIP_Y + STRIP_H / 2);
    } else {
      tft.setTextColor(COL_DIM, COL_TAB);
      tft.drawString(conn ? "ready" : "pair me", 28, STRIP_Y + STRIP_H / 2);
    }
    tft.setTextDatum(textdatum_t::middle_right);
    tft.setFont(&fonts::FreeSansBold9pt7b);
    tft.setTextColor(caseMode == CASE_LOWER ? COL_DIM : COL_TAB_ON, COL_TAB);
    tft.drawString(caseMode == CASE_LOWER ? "abc" : caseMode == CASE_SHIFT ? "Abc" : "ABC",
                   SCREEN_W - 8, STRIP_Y + STRIP_H / 2);
  } else {
    tft.setTextColor(conn ? COL_OK : COL_ADV, COL_TAB);
    tft.drawString(conn ? "linked" : "pair me", 28, STRIP_Y + STRIP_H / 2);
  }
}

// ---------------- T9 view ----------------
static void t9CellRect(int idx, int& x, int& y, int& w, int& h) {
  x = (idx % 3) * T9_COL_W;
  y = AREA_Y + (idx / 3) * T9_ROW_H;
  w = T9_COL_W;
  h = T9_ROW_H;
}

static void drawT9Key(int idx, bool pressed) {
  int x, y, w, h;
  t9CellRect(idx, x, y, w, h);
  bool spec = (idx >= 12 || idx == T9_CASE_KEY);
  uint16_t bg = pressed ? COL_KEY_DOWN : (spec ? COL_KEY_SPEC : COL_KEY);
  tft.fillRoundRect(x + KEY_GAP, y + KEY_GAP, w - 2 * KEY_GAP, h - 2 * KEY_GAP, 6, bg);

  int cx = x + w / 2, cy = y + h / 2;
  if (idx == T9_BKSP)    { drawGlyph(cx, cy, 6, COL_TEXT); return; }
  if (idx == T9_ADVANCE) { drawGlyph(cx, cy, 4, COL_TEXT); return; }
  if (idx == T9_ENTER)   { drawGlyph(cx, cy, 7, COL_TEXT); return; }

  const T9KeyDef& k = T9_KEYS[idx];
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.setTextColor(COL_TEXT, bg);
  tft.drawString(k.big, cx, y + 17);

  char buf[8];
  if (idx == T9_CASE_KEY) {
    strcpy(buf, caseMode == CASE_LOWER ? "abc" : caseMode == CASE_SHIFT ? "Abc" : "ABC");
  } else {
    strncpy(buf, k.small, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    if (caseMode != CASE_LOWER)
      for (char* p = buf; *p; p++) *p = toupper((unsigned char)*p);
  }
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(idx == T9_CASE_KEY ? COL_TAB_ON : COL_DIM, bg);
  tft.drawString(buf, cx, y + h - 14);
}

static void drawT9View() {
  tft.fillRect(0, AREA_Y, SCREEN_W, AREA_H, COL_BG);
  for (int i = 0; i < 15; i++) drawT9Key(i, false);
}

static char t9ApplyCase(char c) {
  if (caseMode != CASE_LOWER && isalpha((unsigned char)c))
    return toupper((unsigned char)c);
  return c;
}

// Send the buffered candidate as one HID character and close the sequence.
static void t9Commit() {
  pendKey = -1;
  if (!pendChar) return;
  char c = pendChar;
  pendChar = 0;
  Serial.printf("[t9] commit '%c'\n", c);
  hidkb_tapChar(c);
  if (caseMode == CASE_SHIFT && isalpha((unsigned char)c)) {
    caseMode = CASE_LOWER;  // one-shot shift consumed
    if (curView == VIEW_KB) { drawT9View(); }
  }
  if (curView == VIEW_KB) drawStrip();
}

// A confirmed tap on a cycle key: start or advance the candidate buffer.
static void t9Tap(int idx) {
  const T9KeyDef& k = T9_KEYS[idx];
  uint32_t now = millis();

  if (idx == pendKey && pendChar && now < pendDeadline) {
    pendIdx = (pendIdx + 1) % strlen(k.cycle);   // cycle in the buffer
  } else {
    t9Commit();                                  // different key interrupts
    pendKey = idx;
    pendIdx = 0;
  }
  pendChar = t9ApplyCase(k.cycle[pendIdx]);
  pendDeadline = now + T9_MULTITAP_MS;
  drawStrip();
}

static void t9TouchDown(int tx, int ty) {
  int col = tx / T9_COL_W;            if (col > 2) col = 2;
  int row = (ty - AREA_Y) / T9_ROW_H; if (row > 4) row = 4;
  int idx = row * 3 + col;

  t9DownCell = idx;
  t9DownTime = millis();
  t9LongDone = false;
  drawT9Key(idx, true);
  Serial.printf("[t9] down cell=%d\n", idx);

  // Act on DOWN: down is the reliable edge on this noisy panel; the touch
  // layer already filters ghosts, so there's nothing left to wait for.
  // Long-press (in ui_tick) can still upgrade a held letter key to its digit.
  switch (idx) {
    case T9_BKSP:
      if (pendChar) { pendChar = 0; pendKey = -1; drawStrip(); }  // drop candidate
      else hidkb_press(0x2A, 0);                                  // real backspace, repeats
      break;
    case T9_ENTER:
      t9Commit();
      hidkb_press(0x28, 0);
      break;
    case T9_ADVANCE:
      t9Commit();                                                 // manual same-key advance
      break;
    case T9_CASE_KEY:
      caseMode = (CaseMode)((caseMode + 1) % 3);
      drawT9View();
      drawStrip();
      break;
    default:
      t9Tap(idx);                                                 // buffer the letter now
      break;
  }
}

static void t9TouchUp() {
  if (t9DownCell < 0) return;
  int idx = t9DownCell;
  t9DownCell = -1;
  drawT9Key(idx, false);
  if (idx == T9_BKSP || idx == T9_ENTER) hidkb_releaseAll();
}

// ---------------- chrome: tabs ----------------
static void drawTabs() {
  for (int i = 0; i < VIEW_COUNT; i++) {
    bool on = (i == (int)curView);
    tft.fillRect(i * TAB_W, 0, TAB_W, TAB_H, on ? COL_TAB_ON : COL_TAB);
    tft.setTextDatum(textdatum_t::middle_center);
    tft.setFont(&fonts::FreeSansBold9pt7b);
    tft.setTextColor(on ? COL_BG : COL_TEXT, on ? COL_TAB_ON : COL_TAB);
    tft.drawString(TAB_LABELS[i], i * TAB_W + TAB_W / 2, TAB_H / 2);
  }
}

// ---------------- generic key views (numpad / nav) ----------------
static void drawKey(const KeyDef* k, int x, int y, int w, int h, bool pressed) {
  uint16_t bg = pressed ? COL_KEY_DOWN
              : (k->action != ACT_NONE ? COL_KEY_SPEC : COL_KEY);
  tft.fillRoundRect(x + KEY_GAP, y + KEY_GAP, w - 2 * KEY_GAP, h - 2 * KEY_GAP, 6, bg);

  int cx = x + w / 2, cy = y + h / 2;
  if (k->label[0] != 0 && k->label[0] < 0x08) {
    drawGlyph(cx, cy, k->label[0], COL_TEXT);
    return;
  }
  if (k->label[0] == 0) return;  // space bar: blank face

  tft.setTextDatum(textdatum_t::middle_center);
  tft.setFont(strlen(k->label) > 2 ? &fonts::FreeSans9pt7b : &fonts::FreeSansBold12pt7b);
  tft.setTextColor(COL_TEXT, bg);
  tft.drawString(k->label, cx, cy);
}

static bool keyRect(const ViewDef* v, int row, int col, int& x, int& y, int& w, int& h) {
  if (!v || row >= v->count) return false;
  const RowDef& r = v->rows[row];
  if (col >= r.count) return false;

  float total = 0;
  for (int i = 0; i < r.count; i++) total += r.keys[i].w;

  int rowH = AREA_H / v->count;
  y = AREA_Y + row * rowH;
  h = rowH;

  float acc = 0;
  for (int i = 0; i < col; i++) acc += r.keys[i].w;
  x = (int)(SCREEN_W * acc / total + 0.5f);
  int x2 = (int)(SCREEN_W * (acc + r.keys[col].w) / total + 0.5f);
  w = x2 - x;
  return true;
}

static void drawView();  // defined below; used by the switch-failed fallback

// ---------------- dual-boot app switch ----------------
// Reboot into another app slot. Marauder lives in ota_0, TouchBoard in ota_1;
// esp_ota_set_boot_partition points the bootloader at the target, esp_restart
// jumps there (~1s). No image is copied — both apps stay resident in flash.
static void switchToApp(esp_partition_subtype_t sub, const char* name) {
  const esp_partition_t* p =
      esp_partition_find_first(ESP_PARTITION_TYPE_APP, sub, nullptr);
  if (!p) { Serial.printf("[switch] %s partition not found\n", name); return; }

  tft.fillScreen(COL_BG);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawString(String("Starting ") + name + "...", SCREEN_W / 2, SCREEN_H / 2);

  esp_err_t err = esp_ota_set_boot_partition(p);
  Serial.printf("[switch] boot -> %s: %s\n", name, esp_err_to_name(err));
  if (err != ESP_OK) {                       // stay put if the target is bad
    tft.setTextColor(COL_ADV, COL_BG);
    tft.drawString("switch failed", SCREEN_W / 2, SCREEN_H / 2 + 26);
    delay(1500);
    drawView();
    return;
  }
  delay(200);
  esp_restart();
}

// ---------------- BT info screen ----------------
static void drawBTButton(const KeyDef* k, int y, bool pressed) {
  tft.fillRoundRect(BT_BTN_X, y, BT_BTN_W, BT_BTN_H, 8, pressed ? COL_KEY_DOWN : COL_KEY_SPEC);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setFont(&fonts::FreeSansBold9pt7b);
  tft.setTextColor(COL_TEXT, pressed ? COL_KEY_DOWN : COL_KEY_SPEC);
  tft.drawString(k->label, BT_BTN_X + BT_BTN_W / 2, y + BT_BTN_H / 2);
}

static void drawBTView() {
  tft.fillRect(0, AREA_Y, SCREEN_W, AREA_H, COL_BG);
  bool conn = hidkb_connected();

  tft.setTextDatum(textdatum_t::top_left);
  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawString("Bluetooth", 12, AREA_Y + 8);

  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.drawString(String("Name: ") + BLE_DEVICE_NAME, 12, AREA_Y + 32);

  tft.setTextColor(conn ? COL_OK : COL_ADV, COL_BG);
  if (conn) {
    tft.drawString("Connected:", 12, AREA_Y + 56);
    tft.drawString(hidkb_peerAddress(), 12, AREA_Y + 76);
  } else {
    tft.drawString("Advertising...", 12, AREA_Y + 56);
  }

  tft.setTextColor(COL_DIM, COL_BG);
  tft.drawString(String("Paired hosts: ") + hidkb_bondCount(), 12, AREA_Y + 96);

  drawBTButton(&BT_BTN_ADV,   BT_BTN1_Y, false);
  drawBTButton(&BT_BTN_CLEAR, BT_BTN2_Y, false);
  drawBTButton(&BT_BTN_EXIT,  BT_BTN3_Y, false);
}

// ---------------- QWERTY (landscape) ----------------
// T9 is charming but slow. This is the "I actually need to type a sentence"
// mode: a full keyboard, and the ONLY screen that flips the panel to landscape
// (320x240). Enter from the QWE tab; the on-key "abc" button drops back to
// portrait T9.
static const int LAND_W = 320, LAND_H = 240;
static const int QW_ROWS = 4;

static CaseMode qwCase = CASE_LOWER;      // tap shift: one-shot -> CAPS lock -> off
static int  qwDownRow = -1, qwDownCol = -1;

enum { QK_CHAR, QK_SHIFT, QK_BKSP, QK_ENTER, QK_BACK, QK_SPACE };
struct QKey { const char* cap; uint8_t type; char ch; float w; };

static const QKey QROW0[] = {
  {"q",QK_CHAR,'q',1},{"w",QK_CHAR,'w',1},{"e",QK_CHAR,'e',1},{"r",QK_CHAR,'r',1},{"t",QK_CHAR,'t',1},
  {"y",QK_CHAR,'y',1},{"u",QK_CHAR,'u',1},{"i",QK_CHAR,'i',1},{"o",QK_CHAR,'o',1},{"p",QK_CHAR,'p',1},
};
static const QKey QROW1[] = {
  {"a",QK_CHAR,'a',1},{"s",QK_CHAR,'s',1},{"d",QK_CHAR,'d',1},{"f",QK_CHAR,'f',1},{"g",QK_CHAR,'g',1},
  {"h",QK_CHAR,'h',1},{"j",QK_CHAR,'j',1},{"k",QK_CHAR,'k',1},{"l",QK_CHAR,'l',1},
};
static const QKey QROW2[] = {
  {"shift",QK_SHIFT,0,1.6f},{"z",QK_CHAR,'z',1},{"x",QK_CHAR,'x',1},{"c",QK_CHAR,'c',1},{"v",QK_CHAR,'v',1},
  {"b",QK_CHAR,'b',1},{"n",QK_CHAR,'n',1},{"m",QK_CHAR,'m',1},{"bksp",QK_BKSP,0,1.6f},
};
static const QKey QROW3[] = {
  {"abc",QK_BACK,0,1.6f},{",",QK_CHAR,',',1},{"space",QK_SPACE,' ',4},{".",QK_CHAR,'.',1},{"enter",QK_ENTER,0,1.6f},
};
static const QKey* const QROWS[QW_ROWS] = { QROW0, QROW1, QROW2, QROW3 };
static const int QROW_N[QW_ROWS] = { 10, 9, 9, 5 };

static void qKeyRect(int r, int c, int& x, int& y, int& w, int& h) {
  const QKey* row = QROWS[r];
  int n = QROW_N[r];
  float total = 0; for (int i = 0; i < n; i++) total += row[i].w;
  int rowH = LAND_H / QW_ROWS;
  y = r * rowH; h = rowH;
  float acc = 0; for (int i = 0; i < c; i++) acc += row[i].w;
  x = (int)(LAND_W * acc / total + 0.5f);
  int x2 = (int)(LAND_W * (acc + row[c].w) / total + 0.5f);
  w = x2 - x;
}

static void drawQKey(int r, int c, bool pressed) {
  const QKey& k = QROWS[r][c];
  int x, y, w, h; qKeyRect(r, c, x, y, w, h);
  bool spec = (k.type != QK_CHAR);
  uint16_t bg = pressed ? COL_KEY_DOWN : (spec ? COL_KEY_SPEC : COL_KEY);
  if (k.type == QK_SHIFT && qwCase != CASE_LOWER && !pressed) bg = COL_TAB_ON;  // lit: shift or caps
  tft.fillRoundRect(x + KEY_GAP, y + KEY_GAP, w - 2 * KEY_GAP, h - 2 * KEY_GAP, 6, bg);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setTextColor(COL_TEXT, bg);
  char cap[8];
  if (k.type == QK_CHAR) {
    cap[0] = (qwCase != CASE_LOWER && k.ch >= 'a' && k.ch <= 'z') ? (char)(k.ch - 32) : k.ch;
    cap[1] = 0;
    tft.setFont(&fonts::FreeSansBold12pt7b);
  } else {
    const char* lbl = (k.type == QK_SHIFT && qwCase == CASE_CAPS) ? "CAPS" : k.cap;
    strncpy(cap, lbl, sizeof(cap) - 1); cap[sizeof(cap) - 1] = 0;
    tft.setFont(&fonts::FreeSans9pt7b);
  }
  tft.drawString(cap, x + w / 2, y + h / 2);
}

static void drawQwerty() {
  tft.fillScreen(COL_BG);
  for (int r = 0; r < QW_ROWS; r++)
    for (int c = 0; c < QROW_N[r]; c++)
      drawQKey(r, c, false);
}

// The panel always reports portrait raw (tx 0..239, ty 0..319) regardless of
// display rotation. This maps it into the setRotation(1) landscape frame.
// Derived to match the confirmed portrait mapping, so it should land aligned.
// If keys read mirrored: swap to lx=ty, ly=239-tx AND setRotation(3).
static void qwertyMap(int tx, int ty, int& lx, int& ly) {
  lx = 319 - ty;
  ly = tx;
  lx = constrain(lx, 0, LAND_W - 1);
  ly = constrain(ly, 0, LAND_H - 1);
}

static bool qKeyAt(int lx, int ly, int& rr, int& cc) {
  int rowH = LAND_H / QW_ROWS;
  int r = ly / rowH; if (r >= QW_ROWS) r = QW_ROWS - 1;
  for (int c = 0; c < QROW_N[r]; c++) {
    int x, y, w, h; qKeyRect(r, c, x, y, w, h);
    if (lx >= x && lx < x + w) { rr = r; cc = c; return true; }
  }
  return false;
}

static void qwertyExit() {          // back to portrait T9
  qwCase = CASE_LOWER;
  qwDownRow = qwDownCol = -1;
  tft.setRotation(2);
  curView = VIEW_KB;
  drawView();
}

static void qwertyTouchDown(int tx, int ty) {
  int lx, ly; qwertyMap(tx, ty, lx, ly);
  int r, c;
  if (!qKeyAt(lx, ly, r, c)) { qwDownRow = qwDownCol = -1; return; }
  qwDownRow = r; qwDownCol = c;
  drawQKey(r, c, true);

  const QKey& k = QROWS[r][c];
  switch (k.type) {
    case QK_CHAR: {
      bool isAlpha = (k.ch >= 'a' && k.ch <= 'z');
      bool upper   = isAlpha && (qwCase != CASE_LOWER);
      hidkb_tapChar(upper ? (char)(k.ch - 32) : k.ch);
      if (isAlpha && qwCase == CASE_SHIFT) { qwCase = CASE_LOWER; drawQwerty(); }  // one-shot spent; CAPS sticks
      break;
    }
    case QK_SHIFT: qwCase = (CaseMode)((qwCase + 1) % 3); drawQwerty(); break;  // shift -> CAPS -> off
    case QK_BKSP:  hidkb_tap(0x2A, 0);                 break;  // Backspace
    case QK_ENTER: hidkb_tap(0x28, 0);                 break;  // Enter
    case QK_SPACE: hidkb_tapChar(' ');                 break;
    case QK_BACK:  qwertyExit();                       break;
  }
}

static void qwertyTouchUp() {
  if (qwDownRow < 0) return;
  int r = qwDownRow, c = qwDownCol;
  qwDownRow = qwDownCol = -1;
  if (curView == VIEW_QWERTY) drawQKey(r, c, false);  // a full redraw may have already happened
}

// ---------------- full redraw ----------------
static void drawView() {
  if (curView == VIEW_QWERTY) { tft.setRotation(1); drawQwerty(); return; }
  tft.setRotation(2);          // every other screen is portrait
  drawTabs();
  // MIDI mode owns everything below the tab bar (no preview strip).
  if (curView == VIEW_MIDI) { midiui_draw(); return; }
  drawStrip();
  if (curView == VIEW_KB) { drawT9View(); return; }
  if (curView == VIEW_BT) { drawBTView(); return; }

  tft.fillRect(0, AREA_Y, SCREEN_W, AREA_H, COL_BG);
  const ViewDef* v = currentViewDef();
  int x, y, w, h;
  for (int r = 0; r < v->count; r++)
    for (int c = 0; c < v->rows[r].count; c++)
      if (keyRect(v, r, c, x, y, w, h))
        drawKey(&v->rows[r].keys[c], x, y, w, h, false);
}

// ---------------- input handling ----------------
void ui_onTouchDown(int tx, int ty) {
  if (curView == VIEW_QWERTY) { qwertyTouchDown(tx, ty); return; }  // landscape, own coords

  down = { nullptr, 0, 0, 0, 0, -1, false };

  // tab bar
  if (ty < TAB_H) {
    int t = tx / TAB_W;
    if (t < VIEW_COUNT) { down.tab = t; down.valid = true; }
    return;
  }

  // MIDI mode owns the whole area below the tab bar (its own sub-tabs +
  // sections). Checked before the strip guard because MIDI has no strip.
  if (curView == VIEW_MIDI) { midiTouchActive = true; midiui_touchDown(tx, ty); return; }

  if (ty < AREA_Y) return;  // strip is display-only

  if (curView == VIEW_KB) { t9TouchDown(tx, ty); return; }

  // BT screen buttons
  if (curView == VIEW_BT) {
    if (tx >= BT_BTN_X && tx < BT_BTN_X + BT_BTN_W) {
      if (ty >= BT_BTN1_Y && ty < BT_BTN1_Y + BT_BTN_H)      { down.k = &BT_BTN_ADV;   down.y = BT_BTN1_Y; }
      else if (ty >= BT_BTN2_Y && ty < BT_BTN2_Y + BT_BTN_H) { down.k = &BT_BTN_CLEAR; down.y = BT_BTN2_Y; }
      else if (ty >= BT_BTN3_Y && ty < BT_BTN3_Y + BT_BTN_H) { down.k = &BT_BTN_EXIT;  down.y = BT_BTN3_Y; }
      if (down.k) {
        down.valid = true;
        drawBTButton(down.k, down.y, true);
      }
    }
    return;
  }

  // generic key grids
  const ViewDef* v = currentViewDef();
  int x, y, w, h;
  for (int r = 0; r < v->count; r++) {
    for (int c = 0; c < v->rows[r].count; c++) {
      if (!keyRect(v, r, c, x, y, w, h)) continue;
      if (tx >= x && tx < x + w && ty >= y && ty < y + h) {
        down = { &v->rows[r].keys[c], x, y, w, h, -1, true };
        drawKey(down.k, x, y, w, h, true);
        if (down.k->action == ACT_NONE)
          hidkb_press(down.k->usage, down.k->mods);
        return;
      }
    }
  }
}

void ui_onTouchUp() {
  if (midiTouchActive) { midiui_touchUp(); midiTouchActive = false; return; }
  if (curView == VIEW_QWERTY) { qwertyTouchUp(); return; }
  if (curView == VIEW_KB && !down.valid) { t9TouchUp(); return; }
  if (!down.valid) return;
  ActiveKey k = down;
  down.valid = false;

  // tab released -> switch view
  if (k.tab >= 0) {
    if ((ViewId)k.tab != curView) {
      t9Commit();
      ViewId prev = curView;
      curView = (ViewId)k.tab;
      if (prev == VIEW_MIDI) midiui_exit();   // hand BLE radio back to HID keyboard
      drawView();
      if (curView == VIEW_MIDI) midiui_enter();  // advertise as MIDI device
    }
    return;
  }
  if (!k.k) return;

  if (k.k->action == ACT_NONE) {
    hidkb_releaseAll();
    drawKey(k.k, k.x, k.y, k.w, k.h, false);
    return;
  }

  switch (k.k->action) {
    case ACT_BT_ADV:   hidkb_restartAdvertising(); drawBTView(); break;
    case ACT_BT_CLEAR: hidkb_clearBonds();         drawBTView(); break;
    case ACT_EXIT_MARAUDER:
      switchToApp(ESP_PARTITION_SUBTYPE_APP_OTA_0, "Marauder");
      break;
  }
}

void ui_onTouchMove(int x, int y) {
  if (midiTouchActive) midiui_touchMove(x, y);
}

void ui_tick(uint32_t now) {
  if (curView == VIEW_MIDI) midiui_tick(now);   // live transport progress/meters

  // T9: multi-tap window expired -> commit the candidate.
  // (Long-press-for-digit was removed: this panel holds "finger down" well
  // past the threshold after a physical lift, so it fired on normal taps and
  // turned every letter into its digit. Digits live on the 123 tab and at the
  // end of each key's cycle, e.g. tap '2' four times -> '2'.)
  if (pendChar && now > pendDeadline && curView == VIEW_KB) t9Commit();

  static uint32_t lastCheck = 0;
  if (now - lastCheck < 300) return;
  lastCheck = now;

  bool conn = hidkb_connected();
  int bonds = hidkb_bondCount();
  if (conn != lastConn || bonds != lastBonds) {
    lastConn = conn;
    lastBonds = bonds;
    if (curView != VIEW_QWERTY && curView != VIEW_MIDI) drawStrip();  // portrait-only, and MIDI has no strip
    if (curView == VIEW_BT) drawBTView();
  }
}

void ui_bootIntoMidi() { s_bootMidi = true; }

void ui_begin() {
  tft.fillScreen(COL_BG);
  if (s_bootMidi) curView = VIEW_MIDI;
  drawView();
  if (curView == VIEW_MIDI) midiui_enter();
}
