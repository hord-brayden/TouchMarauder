#pragma once
#include <stdint.h>
#include "midi_event.h"

// Transport-agnostic MIDI engine. UI code calls the semantic helpers below;
// this module fans each message out to whichever backends are enabled
// (BLE-MIDI and/or USB-serial) and, for LIVE messages, hands a copy to the
// record monitor so the sequencer can capture a take.
//
// Playback (song_player) must use midi_sendRaw() so its notes are NOT
// re-captured while overdubbing.

typedef void (*MidiMonitorFn)(uint8_t status, uint8_t d1, uint8_t d2);

void midi_core_begin();

// Backend enable flags (both can be on at once).
void midi_setBackends(bool ble, bool serialUsb);
bool midi_bleEnabled();
bool midi_serialEnabled();

// Active MIDI channel (0-based).
void    midi_setChannel(uint8_t ch);
uint8_t midi_channel();

// ---- live messages (from the UI): sent to backends AND to the monitor ----
void midi_noteOn(uint8_t note, uint8_t vel);
void midi_noteOff(uint8_t note);
// Explicit-channel variants (drum pads use channel 10 regardless of the
// global channel). Still captured by the recorder via the full status byte.
void midi_noteOnCh(uint8_t ch, uint8_t note, uint8_t vel);
void midi_noteOffCh(uint8_t ch, uint8_t note);
void midi_cc(uint8_t num, uint8_t val);
void midi_pitchBend(int value14);        // 0..16383, 8192 = center
void midi_program(uint8_t prog);

// Silence everything: note-offs for tracked notes + CC123 on all channels.
void midi_allNotesOff();

// ---- low-level send, bypasses the monitor (used by the song player) ----
void midi_sendRaw(uint8_t status, uint8_t d1, uint8_t d2);

// The sequencer registers here to capture live input.
void midi_setMonitor(MidiMonitorFn fn);
