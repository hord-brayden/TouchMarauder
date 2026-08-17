#pragma once
#include <stdint.h>

// One recorded MIDI event. `tMs` is milliseconds from the start of the take;
// on save it is converted to SMF ticks using the song's tempo + PPQ. Only
// channel-voice messages are stored (note on/off, CC, pitch bend, program),
// which is everything this controller can generate.
struct MidiEvent {
  uint32_t tMs;
  uint8_t  status;   // full status byte incl. channel (e.g. 0x90 | ch)
  uint8_t  d1;
  uint8_t  d2;
};

// Length in bytes of a channel-voice message given its status byte.
// 0xC0 (program change) and 0xD0 (channel pressure) carry ONE data byte;
// everything else this project emits carries two.
static inline uint8_t midi_msgLen(uint8_t status) {
  uint8_t hi = status & 0xF0;
  return (hi == 0xC0 || hi == 0xD0) ? 2 : 3;
}
