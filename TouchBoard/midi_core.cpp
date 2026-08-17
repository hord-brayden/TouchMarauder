#include "midi_core.h"
#include "config.h"
#include "midi_ble.h"
#include "midi_serial.h"

static bool          s_ble      = true;
static bool          s_serial   = false;
static uint8_t       s_channel  = MIDI_DEFAULT_CH;
static MidiMonitorFn s_monitor  = nullptr;

// Per-channel active-note bitmap (128 notes) so panic/all-notes-off can send a
// precise note-off for anything left ringing.
static uint8_t s_active[16][16];  // [channel][note/8] bit = note&7

static inline void markNote(uint8_t ch, uint8_t note, bool on) {
  if (ch > 15 || note > 127) return;
  uint8_t& b = s_active[ch][note >> 3];
  if (on) b |= (1 << (note & 7));
  else    b &= ~(1 << (note & 7));
}

void midi_core_begin() {
  for (int c = 0; c < 16; c++)
    for (int i = 0; i < 16; i++) s_active[c][i] = 0;
}

void midi_setBackends(bool ble, bool serialUsb) { s_ble = ble; s_serial = serialUsb; }
bool midi_bleEnabled()    { return s_ble; }
bool midi_serialEnabled() { return s_serial; }

void midi_setChannel(uint8_t ch) { s_channel = ch & 0x0F; }
uint8_t midi_channel()           { return s_channel; }

void midi_setMonitor(MidiMonitorFn fn) { s_monitor = fn; }

void midi_sendRaw(uint8_t status, uint8_t d1, uint8_t d2) {
  // Track note state here (not just in the live path) so playback notes are
  // also in s_active — panic/all-notes-off can then send a real note-off for
  // anything the sequencer left ringing, not just rely on CC123.
  uint8_t hi = status & 0xF0, ch = status & 0x0F;
  if (hi == 0x90 && d2 > 0)                        markNote(ch, d1, true);
  else if (hi == 0x80 || (hi == 0x90 && d2 == 0))  markNote(ch, d1, false);

  uint8_t len = midi_msgLen(status);
  uint8_t buf[3] = { status, d1, d2 };
  if (s_ble)    midi_ble_send(buf, len);
  if (s_serial) midi_serial_send(buf, len);
}

// Live send: emit (which tracks note state) and feed the recorder.
static void liveSend(uint8_t status, uint8_t d1, uint8_t d2) {
  midi_sendRaw(status, d1, d2);
  if (s_monitor) s_monitor(status, d1, d2);
}

void midi_noteOn(uint8_t note, uint8_t vel) {
  if (note > 127) return;
  if (vel == 0) { midi_noteOff(note); return; }
  liveSend(0x90 | s_channel, note, vel & 0x7F);
}

void midi_noteOff(uint8_t note) {
  if (note > 127) return;
  liveSend(0x80 | s_channel, note, 0);
}

void midi_noteOnCh(uint8_t ch, uint8_t note, uint8_t vel) {
  if (note > 127) return;
  if (vel == 0) { midi_noteOffCh(ch, note); return; }
  liveSend(0x90 | (ch & 0x0F), note, vel & 0x7F);
}

void midi_noteOffCh(uint8_t ch, uint8_t note) {
  if (note > 127) return;
  liveSend(0x80 | (ch & 0x0F), note, 0);
}

void midi_cc(uint8_t num, uint8_t val) {
  liveSend(0xB0 | s_channel, num & 0x7F, val & 0x7F);
}

void midi_pitchBend(int value14) {
  if (value14 < 0) value14 = 0; if (value14 > 16383) value14 = 16383;
  liveSend(0xE0 | s_channel, value14 & 0x7F, (value14 >> 7) & 0x7F);
}

void midi_program(uint8_t prog) {
  liveSend(0xC0 | s_channel, prog & 0x7F, 0);
}

void midi_allNotesOff() {
  for (uint8_t ch = 0; ch < 16; ch++) {
    for (uint8_t n = 0; n < 128; n++) {
      if (s_active[ch][n >> 3] & (1 << (n & 7))) {
        midi_sendRaw(0x80 | ch, n, 0);
        markNote(ch, n, false);
      }
    }
    midi_sendRaw(0xB0 | ch, 123, 0);  // All Notes Off
    midi_sendRaw(0xB0 | ch, 120, 0);  // All Sound Off
  }
}
