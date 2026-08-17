#pragma once
#include <stdint.h>
#include <stddef.h>

// USB-serial MIDI backend. Emits raw MIDI bytes on UART0 (the CH340 port the
// board is flashed through). A bridge on the host turns that byte stream into
// a real MIDI port:
//   - Hairless MIDI<->Serial Bridge (GUI), or
//   - the tools/midi_serial_bridge.py script in this repo (mido + pyserial).
//
// UART0 is shared with the debug log, so when this backend is the active
// transport the firmware suppresses its chatter (see midi_serialEnabled()
// checks) to keep the byte stream clean.
void midi_serial_begin();
void midi_serial_send(const uint8_t* bytes, size_t len);
