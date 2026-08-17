#pragma once
#include <FS.h>
#include "midi_event.h"

// Standard MIDI File (format 0) read/write. Times in MidiEvent are absolute
// milliseconds; conversion to/from SMF ticks uses `bpm` and MIDI_SMF_PPQ.
// The reader understands running status and skips meta/sysex it can't play,
// so files exported from a DAW load too (as long as they fit maxEvents).

// Write `n` events as a one-track SMF. Returns false on any write error.
bool smf_write(fs::File& f, const MidiEvent* ev, int n, int bpm);

// Parse an SMF into ev[] (up to maxEvents). Returns the event count, or -1 on
// a malformed header. bpmOut gets the file's initial tempo (120 if none).
int smf_read(fs::File& f, MidiEvent* ev, int maxEvents, int& bpmOut);
