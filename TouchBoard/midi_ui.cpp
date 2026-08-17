//
// MIDI-controller mode UI. Portrait 240x320. The main tab bar (drawn by
// ui.cpp) occupies y 0..28; everything below belongs to this module: a sub-tab
// row at 28..56 and one of six sections beneath it.
//
// Single-touch panel: the piano is monophonic (one note per finger, with
// glissando as you slide). Chords are built by overdub-recording on the REC
// tab, not by pressing two keys at once.
//
#include <Arduino.h>
#include "midi_ui.h"
#include "config.h"
#include "display.h"
#include "midi_core.h"
#include "midi_ble.h"
#include "song.h"
#include "sdstore.h"
#include "hidkb.h"

// ---------------- geometry ----------------
static const int MAIN_TAB_H = 28;                 // matches ui.cpp TAB_H
static const int SUB_Y = 28, SUB_H = 28;
static const int CY = SUB_Y + SUB_H;              // 56: content top
static const int CH = SCREEN_H - CY;              // 264: content height
static const int SUB_N = 6;
static const int SUB_W = SCREEN_W / SUB_N;        // 40

// ---------------- palette ----------------
static const uint16_t C_BG    = 0x0861;
static const uint16_t C_PANEL = 0x2124;
static const uint16_t C_KEY   = 0x39E7;
static const uint16_t C_TEXT  = 0xFFFF;
static const uint16_t C_DIM   = 0x8410;
static const uint16_t C_ACC   = 0x05FF;   // cyan accent
static const uint16_t C_HOT   = 0xF9A0;   // record/active orange-red
static const uint16_t C_OK    = 0x07E0;   // green
static const uint16_t C_WHITE = 0xFFFF;
static const uint16_t C_BLACK = 0x0000;
static const uint16_t C_TABON = 0x04F3;

static const char* NOTE_NAMES[12] =
  {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

enum MSub : uint8_t { MS_KEYS, MS_PADS, MS_CC, MS_REC, MS_SONG, MS_SET };
static const char* SUB_LABELS[SUB_N] = { "KEYS","PADS","CC","REC","SONG","SET" };

// ---------------- state ----------------
static MSub    g_sub      = MS_KEYS;
static int     g_octave   = MIDI_DEFAULT_OCTAVE;
static uint8_t g_velocity = MIDI_DEFAULT_VEL;
static uint8_t g_channel  = MIDI_DEFAULT_CH;

// CC faders
static const uint8_t CC_NUM[4]  = { 1, 7, 10, 74 };
static const char*   CC_NAME[4] = { "Mod","Vol","Pan","Cut" };
static uint8_t       g_cc[4]     = { 0, 100, 64, 64 };  // also the last-sent value (dedup)
static bool          g_sustain   = false;
static int           g_lastBend  = 8192;                // last pitch-bend sent (dedup)

// song browser
static String g_songs[16];
static int    g_songCount = 0;
static int    g_songSel   = -1;
static int    g_songTop   = 0;   // scroll offset into the song list

// gesture tracking (single active touch)
enum GKind : uint8_t { G_NONE, G_KEY, G_PAD, G_FADER, G_BEND, G_VEL };
static GKind g_gk    = G_NONE;
static int   g_gid   = -1;    // fader index, etc.
static int   g_gnote = -1;    // currently sounding note (keys/pads)

// ---------------- small draw helpers ----------------
static void btn(int x,int y,int w,int h,const char* label,bool on,uint16_t accent) {
  tft.fillRoundRect(x, y, w, h, 6, on ? accent : C_KEY);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setFont(&fonts::FreeSansBold9pt7b);
  tft.setTextColor(on ? C_BLACK : C_TEXT, on ? accent : C_KEY);
  tft.drawString(label, x + w/2, y + h/2);
}
static void labelC(int cx,int cy,const char* s,uint16_t fg,uint16_t bg,const lgfx::IFont* f){
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setFont(f);
  tft.setTextColor(fg, bg);
  tft.drawString(s, cx, cy);
}
static void clearContent() { tft.fillRect(0, CY, SCREEN_W, CH, C_BG); }

// ---------------- sub-tab bar ----------------
static void drawSubTabs() {
  for (int i = 0; i < SUB_N; i++) {
    bool on = (i == (int)g_sub);
    tft.fillRect(i*SUB_W, SUB_Y, SUB_W, SUB_H, on ? C_ACC : C_PANEL);
    tft.setTextDatum(textdatum_t::middle_center);
    tft.setFont(&fonts::FreeSansBold9pt7b);
    tft.setTextColor(on ? C_BLACK : C_DIM, on ? C_ACC : C_PANEL);
    tft.drawString(SUB_LABELS[i], i*SUB_W + SUB_W/2, SUB_Y + SUB_H/2);
  }
}

// ================= KEYS =================
static const int WHITE_SEMI[7] = { 0,2,4,5,7,9,11 };     // C D E F G A B
static const int BLACK_SEMI[5] = { 1,3,6,8,10 };          // C# D# F# G# A#
static const int BLACK_AFTER[5]= { 0,1,3,4,5 };           // black sits after this white
static const int KEYS_TOP = CY + 54;                      // control strip is CY..KEYS_TOP
static const int WHITE_W  = SCREEN_W / 7;                 // 34
static const int BLACK_W  = 22;
static const int BLACK_H  = (SCREEN_H - KEYS_TOP) * 3 / 5;

static int keysBaseNote() { return (g_octave + 1) * 12; } // C of the shown octave

static void drawVelSlider() {
  int x = 130, y = CY + 14, w = 100, h = 24;
  tft.fillRoundRect(x, y, w, h, 4, C_PANEL);
  int fill = (g_velocity * w) / 127;
  tft.fillRoundRect(x, y, fill, h, 4, C_ACC);
  char b[16]; snprintf(b, sizeof(b), "Vel %d", g_velocity);
  labelC(x + w/2, y + h/2, b, C_TEXT, fill > w/2 ? C_ACC : C_PANEL, &fonts::FreeSansBold9pt7b);
}

static void drawKeysControl() {
  tft.fillRect(0, CY, SCREEN_W, KEYS_TOP - CY, C_BG);
  btn(2,  CY+8, 34, 34, "-", false, C_KEY);
  char ob[8]; snprintf(ob, sizeof(ob), "O%d", g_octave);
  labelC(58, CY+25, ob, C_TEXT, C_BG, &fonts::FreeSansBold12pt7b);
  btn(84, CY+8, 34, 34, "+", false, C_KEY);
  drawVelSlider();
}

static void drawPiano() {
  // white keys
  for (int i = 0; i < 7; i++) {
    int x = i * WHITE_W;
    tft.fillRect(x, KEYS_TOP, WHITE_W-1, SCREEN_H-KEYS_TOP, C_WHITE);
    int note = keysBaseNote() + WHITE_SEMI[i];
    char nb[8]; snprintf(nb, sizeof(nb), "%s%d", NOTE_NAMES[note%12], note/12 - 1);
    tft.setTextDatum(textdatum_t::bottom_center);
    tft.setFont(&fonts::FreeSans9pt7b);
    tft.setTextColor(C_DIM, C_WHITE);
    tft.drawString(nb, x + WHITE_W/2, SCREEN_H - 4);
  }
  // black keys overlaid
  for (int i = 0; i < 5; i++) {
    int x = (BLACK_AFTER[i]+1) * WHITE_W - BLACK_W/2;
    tft.fillRect(x, KEYS_TOP, BLACK_W, BLACK_H, C_BLACK);
  }
}

static void drawKeys() {
  clearContent();
  drawKeysControl();
  drawPiano();
}

// return midi note for a piano touch, or -1
static int keyAt(int x, int y) {
  if (y < KEYS_TOP) return -1;
  if (y < KEYS_TOP + BLACK_H) {              // black-key band: check blacks first
    for (int i = 0; i < 5; i++) {
      int bx = (BLACK_AFTER[i]+1) * WHITE_W - BLACK_W/2;
      if (x >= bx && x < bx + BLACK_W) return keysBaseNote() + BLACK_SEMI[i];
    }
  }
  int i = x / WHITE_W; if (i > 6) i = 6;
  return keysBaseNote() + WHITE_SEMI[i];
}

static void keysDown(int x, int y) {
  // control strip
  if (y < KEYS_TOP) {
    if (y >= CY+8 && y < CY+42) {
      if (x >= 2 && x < 36)  { if (g_octave > 0) g_octave--; drawKeys(); return; }
      if (x >= 84 && x < 118){ if (g_octave < 8) g_octave++; drawKeys(); return; }  // cap 8: octave 9 would exceed note 127
    }
    if (x >= 130 && x <= 230 && y >= CY+14 && y <= CY+38) {  // vel slider
      g_velocity = constrain((x-130)*127/100, 1, 127);
      g_gk = G_VEL; drawVelSlider(); return;
    }
    return;
  }
  int n = keyAt(x, y);
  if (n < 0) return;
  midi_noteOn(n, g_velocity);
  g_gk = G_KEY; g_gnote = n;
}

static void keysMove(int x, int y) {
  if (g_gk == G_VEL) {
    g_velocity = constrain((x-130)*127/100, 1, 127);
    drawVelSlider(); return;
  }
  if (g_gk != G_KEY) return;
  int n = keyAt(x, y);
  if (n >= 0 && n != g_gnote) {           // glissando
    midi_noteOff(g_gnote);
    midi_noteOn(n, g_velocity);
    g_gnote = n;
  }
}

// ================= PADS =================
static const char* PAD_LBL[16] =
  {"Kick","Rim","Snr","Clap","ESn","LFT","CHH","HFT","PHH","LT","OHH","LMT","HMT","Cr1","HT","Rd1"};
static const int PAD_CH = 9;   // GM drum channel 10 (0-based 9)

static int padNote(int row, int col) { return 36 + (3-row)*4 + col; }  // bottom-left = 36

static void drawPad(int row, int col, bool on) {
  int w = SCREEN_W/4, h = CH/4;
  int x = col*w, y = CY + row*h;
  tft.fillRoundRect(x+2, y+2, w-4, h-4, 6, on ? C_HOT : C_PANEL);
  int note = padNote(row, col);
  int idx = note - 36;
  labelC(x+w/2, y+h/2-6, (idx>=0&&idx<16)?PAD_LBL[idx]:"", on?C_BLACK:C_TEXT, on?C_HOT:C_PANEL, &fonts::FreeSansBold9pt7b);
  char nb[8]; snprintf(nb, sizeof(nb), "%s%d", NOTE_NAMES[note%12], note/12-1);
  labelC(x+w/2, y+h/2+12, nb, on?C_BLACK:C_DIM, on?C_HOT:C_PANEL, &fonts::FreeSans9pt7b);
}

static void drawPads() {
  clearContent();
  for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) drawPad(r, c, false);
}

static int padAt(int x, int y, int& row, int& col) {
  if (y < CY) return -1;
  col = x / (SCREEN_W/4); row = (y - CY) / (CH/4);
  if (col > 3) col = 3; if (row > 3) row = 3;
  return padNote(row, col);
}

static void padsDown(int x, int y) {
  int r,c; int n = padAt(x,y,r,c);
  if (n < 0) return;
  midi_noteOnCh(PAD_CH, n, g_velocity);
  g_gk = G_PAD; g_gnote = n;
  drawPad(r, c, true);
}

// ================= CC =================
static const int FAD_TOP = CY + 28, FAD_BOT = CY + 180, FAD_H = FAD_BOT - FAD_TOP;
static const int BEND_Y = CY + 200, BEND_H = 34;

static void drawFader(int i) {
  int w = SCREEN_W/4, x = i*w;
  int trackX = x + w/2 - 8;
  tft.fillRect(x, FAD_TOP-2, w, FAD_H+4, C_BG);
  tft.fillRoundRect(trackX, FAD_TOP, 16, FAD_H, 4, C_PANEL);
  int fill = g_cc[i] * FAD_H / 127;
  tft.fillRoundRect(trackX, FAD_BOT-fill, 16, fill, 4, C_ACC);
  labelC(x+w/2, CY+12, CC_NAME[i], C_TEXT, C_BG, &fonts::FreeSansBold9pt7b);
  char vb[8]; snprintf(vb, sizeof(vb), "%d", g_cc[i]);
  labelC(x+w/2, FAD_BOT+12, vb, C_DIM, C_BG, &fonts::FreeSans9pt7b);
}

static void drawBend(int center) {
  tft.fillRect(0, BEND_Y-2, SCREEN_W, BEND_H+16, C_BG);
  tft.fillRoundRect(4, BEND_Y, SCREEN_W-8, BEND_H, 6, C_PANEL);
  int cx = center;
  tft.fillRect(cx-3, BEND_Y, 6, BEND_H, C_ACC);
  tft.drawFastVLine(SCREEN_W/2, BEND_Y, BEND_H, C_DIM);   // center mark
  labelC(SCREEN_W/2, BEND_Y+BEND_H+8, "PITCH BEND (springs back)", C_DIM, C_BG, &fonts::FreeSans9pt7b);
}

static void drawCC() {
  clearContent();
  for (int i = 0; i < 4; i++) drawFader(i);
  drawBend(SCREEN_W/2);
  btn(SCREEN_W-72, CY+2, 70, 22, g_sustain?"SUS ON":"SUS", g_sustain, C_OK);
}

static void ccDown(int x, int y) {
  if (y >= CY+2 && y < CY+24 && x >= SCREEN_W-72) {   // sustain toggle
    g_sustain = !g_sustain;
    midi_cc(64, g_sustain ? 127 : 0);
    btn(SCREEN_W-72, CY+2, 70, 22, g_sustain?"SUS ON":"SUS", g_sustain, C_OK);
    return;
  }
  if (y >= FAD_TOP-4 && y <= FAD_BOT+4) {
    int i = x / (SCREEN_W/4); if (i>3) i=3;
    int v = constrain((FAD_BOT - y) * 127 / FAD_H, 0, 127);
    if (v != g_cc[i]) { g_cc[i] = v; midi_cc(CC_NUM[i], v); drawFader(i); }
    g_gk = G_FADER; g_gid = i; return;
  }
  if (y >= BEND_Y && y <= BEND_Y+BEND_H) {
    g_gk = G_BEND;
    int v = constrain((x-4)*16383/(SCREEN_W-8), 0, 16383);
    if (v != g_lastBend) { g_lastBend = v; midi_pitchBend(v); drawBend(x); }
    return;
  }
}

// Only emit on an actual value change: the touch layer fires move events every
// ~8ms poll even for a still finger, and unconditional sends flood BLE and
// blow through the record buffer.
static void ccMove(int x, int y) {
  if (g_gk == G_FADER && g_gid >= 0) {
    int v = constrain((FAD_BOT - y) * 127 / FAD_H, 0, 127);
    if (v != g_cc[g_gid]) { g_cc[g_gid] = v; midi_cc(CC_NUM[g_gid], v); drawFader(g_gid); }
  } else if (g_gk == G_BEND) {
    int cx = constrain(x, 4, SCREEN_W-4);
    int v = constrain((cx-4)*16383/(SCREEN_W-8), 0, 16383);
    if (v != g_lastBend) { g_lastBend = v; midi_pitchBend(v); drawBend(cx); }
  }
}

// ================= REC =================
// Row bands (all relative to content top CY):
//   transport  6..48   toggles 52..88   bpm 92..124
//   remix     128..160  info/progress 164..206  save 212..248
static void drawTransportButtons() {
  SongState st = song_state();
  btn(4,   CY+6,  74, 42, "REC",  st==SONG_RECORDING, C_HOT);
  btn(83,  CY+6,  74, 42, "PLAY", st==SONG_PLAYING,   C_OK);
  btn(162, CY+6,  74, 42, "STOP", st==SONG_STOPPED,   C_ACC);
  btn(4,   CY+52, 74, 36, "LOOP", song_loop(),  C_ACC);
  btn(83,  CY+52, 74, 36, "METRO",song_metro(), C_ACC);
  btn(162, CY+52, 74, 36, "CLEAR",false,        C_KEY);
  btn(4,   CY+92, 50, 32, "-",   false, C_KEY);
  char bb[16]; snprintf(bb, sizeof(bb), "%d BPM", song_bpm());
  labelC(120, CY+108, bb, C_TEXT, C_BG, &fonts::FreeSansBold12pt7b);
  btn(186, CY+92, 50, 32, "+",   false, C_KEY);
  // remix row: transpose down/up a semitone, quantize to 1/16.
  btn(4,   CY+128, 58, 32, "Tr-", false, C_KEY);
  btn(66,  CY+128, 58, 32, "Tr+", false, C_KEY);
  btn(128, CY+128, 108,32, "Quantize", false, C_KEY);
}

static void drawRecInfo() {
  tft.fillRect(0, CY+164, SCREEN_W, 44, C_BG);
  char ib[48];
  snprintf(ib, sizeof(ib), "%d events   %lu.%lus",
           song_eventCount(), (unsigned long)(song_lengthMs()/1000), (song_lengthMs()%1000)/100);
  labelC(SCREEN_W/2, CY+176, ib, C_DIM, C_BG, &fonts::FreeSans9pt7b);
  tft.fillRoundRect(10, CY+190, SCREEN_W-20, 14, 4, C_PANEL);
  int w = (int)((SCREEN_W-20) * song_progress());
  tft.fillRoundRect(10, CY+190, w, 14, 4, C_OK);
}

static void drawRec() {
  clearContent();
  drawTransportButtons();
  drawRecInfo();
  btn(30, CY+212, SCREEN_W-60, 36, "SAVE TAKE", false, C_ACC);
}

static int nextTakeNumber() {
  int best = 0;
  for (int i = 0; i < g_songCount; i++)
    if (g_songs[i].startsWith("take-")) best = max(best, (int)g_songs[i].substring(5).toInt());
  return best + 1;
}

static void recDown(int x, int y) {
  if (y >= CY+6 && y < CY+48) {                       // transport
    if (x < 79)       song_record();
    else if (x < 158) song_play();
    else              song_stop();
    drawRec(); return;
  }
  if (y >= CY+52 && y < CY+88) {                      // loop / metro / clear
    if (x < 79)       song_toggleLoop();
    else if (x < 158) song_toggleMetro();
    else              song_clear();
    drawRec(); return;
  }
  if (y >= CY+92 && y < CY+124) {                     // bpm -/+
    if (x < 54)        song_setBpm(song_bpm()-1);
    else if (x >= 186) song_setBpm(song_bpm()+1);
    drawTransportButtons(); return;
  }
  if (y >= CY+128 && y < CY+160) {                    // remix
    if (x < 62)       song_transpose(-1);
    else if (x < 124) song_transpose(+1);
    else              song_quantize(4);               // snap to 1/16
    drawRec(); return;
  }
  if (y >= CY+212 && y < CY+248) {                    // save take
    // Stop first: a take in progress is two unsorted runs, and saving it that
    // way collapses events onto one tick. Stopping sorts/merges the take.
    if (song_state() != SONG_STOPPED) song_stop();
    g_songCount = sd_listSongs(g_songs, 16);          // refresh count so numbering is correct
    char name[16]; snprintf(name, sizeof(name), "take-%03d", nextTakeNumber());
    bool ok = song_save(name);
    if (ok) g_songSel = -1;
    labelC(SCREEN_W/2, CY+230, ok ? "saved" : "SAVE FAILED (no SD?)",
           ok?C_OK:C_HOT, C_BG, &fonts::FreeSansBold9pt7b);
  }
}

// ================= SONG =================
static const int SONG_ROWS = 6;   // visible rows; the list scrolls past this

static void refreshSongs() {
  g_songCount = sd_listSongs(g_songs, 16);
  if (g_songSel >= g_songCount) g_songSel = -1;
  int maxTop = (g_songCount > SONG_ROWS) ? g_songCount - SONG_ROWS : 0;
  if (g_songTop > maxTop) g_songTop = maxTop;
  if (g_songTop < 0) g_songTop = 0;
}

static void drawSong() {
  clearContent();
  char hb[48];
  if (sd_available()) snprintf(hb, sizeof(hb), "Songs %d  (%llu KiB free)", g_songCount, (unsigned long long)sd_freeKB());
  else                snprintf(hb, sizeof(hb), "No SD card");
  labelC(SCREEN_W/2, CY+12, hb, C_TEXT, C_BG, &fonts::FreeSansBold9pt7b);

  int rowY = CY+26, rowH = 26;
  for (int r = 0; r < SONG_ROWS; r++) {
    int idx = g_songTop + r, y = rowY + r*rowH;
    if (idx >= g_songCount) { tft.fillRect(6, y, SCREEN_W-12, rowH-3, C_BG); continue; }
    bool sel = (idx == g_songSel);
    tft.fillRoundRect(6, y, SCREEN_W-12, rowH-3, 4, sel?C_ACC:C_PANEL);
    tft.setTextDatum(textdatum_t::middle_left);
    tft.setFont(&fonts::FreeSans9pt7b);
    tft.setTextColor(sel?C_BLACK:C_TEXT, sel?C_ACC:C_PANEL);
    tft.drawString(g_songs[idx], 14, y + (rowH-3)/2);
  }
  if (g_songCount == 0 && sd_available())
    labelC(SCREEN_W/2, CY+70, "no .mid files yet", C_DIM, C_BG, &fonts::FreeSans9pt7b);

  btn(4,   CY+186, 74, 34, "Up",      false, C_KEY);
  btn(83,  CY+186, 74, 34, "Dn",      false, C_KEY);
  btn(162, CY+186, 74, 34, "Refresh", false, C_KEY);
  btn(4,   CY+224, 110, 36, "LOAD",   false, C_OK);
  btn(124, CY+224, 112, 36, "DELETE", false, C_HOT);
}

static void songDown(int x, int y) {
  int rowY = CY+26, rowH = 26;
  for (int r = 0; r < SONG_ROWS; r++) {
    int idx = g_songTop + r;
    if (idx < g_songCount && y >= rowY+r*rowH && y < rowY+r*rowH+rowH-3) { g_songSel = idx; drawSong(); return; }
  }
  if (y >= CY+186 && y < CY+220) {                  // scroll / refresh
    if (x < 79)       { if (g_songTop > 0) g_songTop--; drawSong(); }
    else if (x < 158) { if (g_songTop + SONG_ROWS < g_songCount) g_songTop++; drawSong(); }
    else              { refreshSongs(); drawSong(); }
    return;
  }
  if (y >= CY+224 && y < CY+260) {                  // load / delete
    if (x < 118) { if (g_songSel >= 0) { song_load(g_songs[g_songSel].c_str()); g_sub = MS_REC; drawSubTabs(); drawRec(); } }
    else         { if (g_songSel >= 0) { sd_deleteSong(g_songs[g_songSel].c_str()); g_songSel = -1; refreshSongs(); drawSong(); } }
    return;
  }
}

// ================= SET =================
static void drawSet() {
  clearContent();
  int y = CY+8;
  // channel
  btn(6, y, 40, 34, "-", false, C_KEY);
  char cb[12]; snprintf(cb, sizeof(cb), "Ch %d", g_channel+1);
  labelC(120, y+17, cb, C_TEXT, C_BG, &fonts::FreeSansBold12pt7b);
  btn(SCREEN_W-46, y, 40, 34, "+", false, C_KEY);
  y += 42;
  // octave
  btn(6, y, 40, 34, "-", false, C_KEY);
  char ob[12]; snprintf(ob, sizeof(ob), "Oct %d", g_octave);
  labelC(120, y+17, ob, C_TEXT, C_BG, &fonts::FreeSansBold12pt7b);
  btn(SCREEN_W-46, y, 40, 34, "+", false, C_KEY);
  y += 42;
  // velocity
  btn(6, y, 40, 34, "-", false, C_KEY);
  char vb[12]; snprintf(vb, sizeof(vb), "Vel %d", g_velocity);
  labelC(120, y+17, vb, C_TEXT, C_BG, &fonts::FreeSansBold12pt7b);
  btn(SCREEN_W-46, y, 40, 34, "+", false, C_KEY);
  y += 44;
  // transport toggles
  btn(6,   y, 110, 36, midi_bleEnabled()?"BLE: ON":"BLE: off",    midi_bleEnabled(),    C_OK);
  btn(124, y, 110, 36, midi_serialEnabled()?"USB: ON":"USB: off", midi_serialEnabled(), C_OK);
  y += 44;
  btn(6, y, SCREEN_W-12, 38, "PANIC (all notes off)", false, C_HOT);
  y += 46;
  bool sub = midi_ble_subscribed();
  labelC(SCREEN_W/2, y+8, sub ? "BLE-MIDI: host listening" : "BLE-MIDI: waiting for host",
         sub?C_OK:C_DIM, C_BG, &fonts::FreeSans9pt7b);
  labelC(SCREEN_W/2, y+26,
         midi_serialEnabled() ? "USB: run the bridge on the Mac" : "USB off: logs stay on",
         C_DIM, C_BG, &fonts::FreeSans9pt7b);
}

static void setDown(int x, int y) {
  int yy = CY+8;
  if (y >= yy && y < yy+34) {                                  // channel
    if (x < 46)              { if (g_channel>0)  g_channel--; }
    else if (x >= SCREEN_W-46){ if (g_channel<15) g_channel++; }
    else return;
    midi_setChannel(g_channel); drawSet(); return;
  }
  yy += 42;
  if (y >= yy && y < yy+34) {                                  // octave
    if (x < 46)              { if (g_octave>0) g_octave--; }
    else if (x >= SCREEN_W-46){ if (g_octave<8) g_octave++; }  // cap 8 (see keysDown)
    else return;
    drawSet(); return;
  }
  yy += 42;
  if (y >= yy && y < yy+34) {                                  // velocity
    if (x < 46)              { if (g_velocity>1)   g_velocity--; }
    else if (x >= SCREEN_W-46){ if (g_velocity<127) g_velocity++; }
    else return;
    drawSet(); return;
  }
  yy += 44;
  if (y >= yy && y < yy+36) {                                  // transport toggles
    if (x < 118) midi_setBackends(!midi_bleEnabled(), midi_serialEnabled());
    else         midi_setBackends(midi_bleEnabled(), !midi_serialEnabled());
    drawSet(); return;
  }
  yy += 44;
  if (y >= yy && y < yy+38) { midi_allNotesOff(); }             // panic
}

// ---------------- section dispatch ----------------
static void drawSection() {
  switch (g_sub) {
    case MS_KEYS: drawKeys(); break;
    case MS_PADS: drawPads(); break;
    case MS_CC:   drawCC();   break;
    case MS_REC:  drawRec();  break;
    case MS_SONG: refreshSongs(); drawSong(); break;
    case MS_SET:  drawSet();  break;
  }
}

// ---------------- public API ----------------
void midiui_begin() {
  midi_core_begin();
  song_begin();
  midi_setBackends(true, false);     // BLE on, USB off until the user starts the bridge
  midi_setChannel(g_channel);
}

void midiui_enter() {
  midi_ble_advertise();              // become discoverable as a MIDI device
  drawSubTabs();
  drawSection();
}

void midiui_exit() {
  midi_allNotesOff();
  g_gk = G_NONE; g_gnote = -1;
  hidkb_advertiseHID();              // hand the radio back to the keyboard profile
}

void midiui_draw() {                 // called by ui.cpp on a full redraw
  drawSubTabs();
  drawSection();
}

void midiui_touchDown(int x, int y) {
  g_gk = G_NONE; g_gid = -1;
  if (y >= SUB_Y && y < SUB_Y + SUB_H) {          // sub-tab switch
    int t = x / SUB_W;
    if (t < SUB_N && (MSub)t != g_sub) { g_sub = (MSub)t; drawSubTabs(); drawSection(); }
    return;
  }
  switch (g_sub) {
    case MS_KEYS: keysDown(x,y); break;
    case MS_PADS: padsDown(x,y); break;
    case MS_CC:   ccDown(x,y);   break;
    case MS_REC:  recDown(x,y);  break;
    case MS_SONG: songDown(x,y); break;
    case MS_SET:  setDown(x,y);  break;
  }
}

void midiui_touchMove(int x, int y) {
  switch (g_sub) {
    case MS_KEYS: keysMove(x,y); break;
    case MS_CC:   ccMove(x,y);   break;
    default: break;   // pads/rec/song/set act on discrete taps
  }
}

void midiui_touchUp() {
  switch (g_gk) {
    case G_KEY: if (g_gnote >= 0) midi_noteOff(g_gnote); break;
    case G_PAD:
      if (g_gnote >= 0) {
        midi_noteOffCh(PAD_CH, g_gnote);
        int idx = g_gnote - 36;
        if (idx >= 0 && idx < 16) drawPad(3 - idx/4, idx%4, false);
      }
      break;
    case G_BEND: midi_pitchBend(8192); g_lastBend = 8192; drawBend(SCREEN_W/2); break;  // spring to center
    default: break;
  }
  g_gk = G_NONE; g_gnote = -1; g_gid = -1;
}

void midiui_tick(uint32_t now) {
  if (g_sub != MS_REC) return;
  static uint32_t last = 0;
  if (now - last < 120) return;
  last = now;
  if (song_state() != SONG_STOPPED) drawRecInfo();   // live progress/counter
}
