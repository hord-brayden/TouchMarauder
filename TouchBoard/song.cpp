#include <Arduino.h>
#include <algorithm>
#include <SD.h>
#include "song.h"
#include "config.h"
#include "midi_core.h"
#include "midi_event.h"
#include "smf.h"
#include "sdstore.h"

static MidiEvent s_ev[MIDI_MAX_EVENTS];
static int       s_count   = 0;
static SongState s_state   = SONG_STOPPED;
static uint32_t  s_origin  = 0;    // t0 for both playback and recording
static int       s_playIdx = 0;
static int       s_playLimit = 0;  // player only touches events [0, s_playLimit)
static uint32_t  s_lenMs   = 0;
static int       s_bpm     = MIDI_DEFAULT_BPM;
static bool      s_loop    = false;
static bool      s_metro   = false;
static int32_t   s_lastBeat = -1;

static void recomputeLength() {
  s_lenMs = s_count ? s_ev[s_count - 1].tMs + 1 : 0;
}

// midi_core hands us every LIVE message here; capture while recording.
static void captureMonitor(uint8_t status, uint8_t d1, uint8_t d2) {
  if (s_state != SONG_RECORDING) return;
  if (s_count >= MIDI_MAX_EVENTS) return;
  s_ev[s_count].tMs    = millis() - s_origin;
  s_ev[s_count].status = status;
  s_ev[s_count].d1     = d1;
  s_ev[s_count].d2     = d2;
  s_count++;
}

void song_begin() {
  midi_setMonitor(captureMonitor);
}

static void startTransport(SongState st) {
  s_origin   = millis();
  s_playIdx  = 0;
  s_lastBeat = -1;
  s_state    = st;
}

void song_record() {
  if (s_state != SONG_STOPPED) return;
  recomputeLength();
  // The player replays only the events that existed when recording began (the
  // backing track), never the notes being captured now — those were already
  // heard live, and replaying them would double every note.
  s_playLimit = s_count;
  startTransport(SONG_RECORDING);
}

void song_play() {
  if (s_state != SONG_STOPPED || s_count == 0) return;
  recomputeLength();
  s_playLimit = s_count;
  startTransport(SONG_PLAYING);
}

void song_stop() {
  bool wasRec = (s_state == SONG_RECORDING);
  s_state = SONG_STOPPED;
  midi_allNotesOff();
  if (wasRec) {
    // Overdubbed events were appended out of order; sort so playback interleaves.
    std::stable_sort(s_ev, s_ev + s_count,
                     [](const MidiEvent& a, const MidiEvent& b){ return a.tMs < b.tMs; });
  }
  recomputeLength();
}

SongState song_state() { return s_state; }
void song_toggleLoop() { s_loop = !s_loop; }
bool song_loop()       { return s_loop; }

void song_clear() {
  song_stop();
  s_count = 0;
  recomputeLength();
}

int      song_eventCount() { return s_count; }
uint32_t song_lengthMs()   { return s_lenMs; }

float song_progress() {
  if (s_state == SONG_STOPPED || s_lenMs == 0) return 0.0f;
  float p = (float)(millis() - s_origin) / (float)s_lenMs;
  return p < 0 ? 0 : (p > 1 ? 1 : p);
}

void song_setBpm(int bpm) {
  if (bpm < 30) bpm = 30; if (bpm > 300) bpm = 300;
  if (s_count && s_bpm > 0 && bpm != s_bpm) {
    // Rescale the take so a tempo change actually changes playback speed.
    float factor = (float)s_bpm / (float)bpm;   // faster bpm -> shorter times
    for (int i = 0; i < s_count; i++) s_ev[i].tMs = (uint32_t)(s_ev[i].tMs * factor + 0.5f);
    recomputeLength();
  }
  s_bpm = bpm;
}
int  song_bpm() { return s_bpm; }

void song_toggleMetro() { s_metro = !s_metro; }
bool song_metro()       { return s_metro; }

void song_transpose(int semis) {
  for (int i = 0; i < s_count; i++) {
    uint8_t hi = s_ev[i].status & 0xF0;
    if (hi == 0x80 || hi == 0x90) {
      int n = (int)s_ev[i].d1 + semis;
      s_ev[i].d1 = (uint8_t)(n < 0 ? 0 : (n > 127 ? 127 : n));
    }
  }
}

void song_quantize(int stepsPerBeat) {
  if (stepsPerBeat < 1 || s_bpm <= 0) return;
  float stepMs = (60000.0f / s_bpm) / stepsPerBeat;
  for (int i = 0; i < s_count; i++)
    s_ev[i].tMs = (uint32_t)((s_ev[i].tMs / stepMs) + 0.5f) * (uint32_t)(stepMs + 0.5f);
  std::stable_sort(s_ev, s_ev + s_count,
                   [](const MidiEvent& a, const MidiEvent& b){ return a.tMs < b.tMs; });
  recomputeLength();
}

// Scheduler + metronome. Called every loop.
void song_tick(uint32_t now) {
  if (s_state == SONG_STOPPED) return;
  uint32_t elapsed = now - s_origin;

  // Play backing events whose time has arrived. s_playLimit excludes anything
  // being recorded right now (see song_record).
  while (s_playIdx < s_playLimit && s_ev[s_playIdx].tMs <= elapsed) {
    const MidiEvent& e = s_ev[s_playIdx];
    midi_sendRaw(e.status, e.d1, e.d2);
    s_playIdx++;
  }

  // Metronome click on each beat.
  if (s_metro && s_bpm > 0) {
    int32_t beat = (int32_t)(elapsed / (60000UL / s_bpm));
    if (beat != s_lastBeat) {
      s_lastBeat = beat;
      tone(PIN_SPEAKER, (beat % 4 == 0) ? 2200 : 1400, 18);  // accent the downbeat
    }
  }

  // End of playback: loop or stop. (Looping applies to PLAYING only; a take in
  // progress keeps its record clock running so overdubs land at absolute time.)
  if (s_state == SONG_PLAYING && s_playIdx >= s_playLimit) {
    if (s_loop && s_lenMs > 0) {
      midi_allNotesOff();
      s_origin = now; s_playIdx = 0; s_lastBeat = -1;   // wrap (limit stays full)
    } else {
      song_stop();
    }
  }
}

bool song_save(const char* basename) {
  if (!sd_available()) return false;
  String path = sd_songPath(basename);
  File f = SD.open(path, FILE_WRITE);
  if (!f) { Serial.printf("[song] can't open %s\n", path.c_str()); return false; }
  bool ok = smf_write(f, s_ev, s_count, s_bpm);
  f.close();
  if (!midi_serialEnabled())
    Serial.printf("[song] save %s: %s (%d events)\n", path.c_str(), ok ? "ok" : "FAIL", s_count);
  return ok;
}

bool song_load(const char* basename) {
  if (!sd_available()) return false;
  song_stop();
  String path = sd_songPath(basename);
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  int bpm;
  int n = smf_read(f, s_ev, MIDI_MAX_EVENTS, bpm);
  f.close();
  // n < 0: malformed header. n == 0: nothing playable. In both cases keep the
  // current in-RAM take rather than clobbering it with an empty one.
  if (n <= 0) { Serial.printf("[song] %s: no playable events\n", path.c_str()); return false; }
  s_count = n;
  s_bpm   = (bpm < 30) ? 30 : (bpm > 300 ? 300 : bpm);  // clamp: a bad tempo would /0 the metronome
  recomputeLength();
  if (!midi_serialEnabled())
    Serial.printf("[song] load %s: %d events @ %d bpm\n", path.c_str(), n, s_bpm);
  return true;
}
