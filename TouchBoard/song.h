#pragma once
#include <stdint.h>

// The sequencer: a RAM take of up to MIDI_MAX_EVENTS, a scheduler that plays it
// back in real time, and the "remix" edits (transpose / quantize / tempo). It
// registers as midi_core's monitor to capture live input, and plays back via
// midi_sendRaw() so playback is never re-captured during overdub.

enum SongState : uint8_t { SONG_STOPPED, SONG_PLAYING, SONG_RECORDING };

void song_begin();
void song_tick(uint32_t now);     // call every loop: drives playback + metronome

// ---- transport ----
void      song_record();          // empty take -> fresh record; else overdub (play + record)
void      song_play();
void      song_stop();
SongState song_state();
void      song_toggleLoop();
bool      song_loop();

// ---- content ----
void     song_clear();
int      song_eventCount();
uint32_t song_lengthMs();
float    song_progress();         // 0..1 during playback, else 0

// ---- tempo + metronome ----
void song_setBpm(int bpm);        // rescales existing take so playback speed tracks bpm
int  song_bpm();
void song_toggleMetro();
bool song_metro();

// ---- remix edits ----
void song_transpose(int semis);   // shift every note by semitones (clamped 0..127)
void song_quantize(int stepsPerBeat);  // snap event times to a grid (e.g. 4 = 1/16)

// ---- SD ----
bool song_save(const char* basename);
bool song_load(const char* basename);
