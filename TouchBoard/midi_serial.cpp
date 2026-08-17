#include <Arduino.h>
#include "midi_serial.h"
#include "config.h"

// Serial was already begun at 115200 in setup(); MIDI_SERIAL_BAUD matches, so
// there is nothing to reconfigure. Kept as a hook in case the baud diverges.
void midi_serial_begin() {
  if ((uint32_t)MIDI_SERIAL_BAUD != 115200UL) {
    Serial.flush();
    Serial.begin(MIDI_SERIAL_BAUD);
  }
}

void midi_serial_send(const uint8_t* bytes, size_t len) {
  Serial.write(bytes, len);
}
