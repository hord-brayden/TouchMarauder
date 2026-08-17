#pragma once
#include <stdint.h>

// The MIDI-controller mode. Owns everything below the main tab bar when the
// MIDI view is active: a sub-tab row (KEYS / PADS / CC / REC / SONG / SET) and
// the section under it. ui.cpp delegates drawing + touch here; entering the
// view switches BLE advertising to MIDI, leaving it restores the HID keyboard.

void midiui_begin();                       // one-time init (backends, sd, sequencer)
void midiui_enter();                       // MIDI view gained focus
void midiui_exit();                        // MIDI view lost focus (back to keyboard)
void midiui_draw();                        // full draw of sub-tabs + current section
void midiui_touchDown(int x, int y);
void midiui_touchMove(int x, int y);       // finger dragged while down
void midiui_touchUp();
void midiui_tick(uint32_t now);            // animate transport/meters
