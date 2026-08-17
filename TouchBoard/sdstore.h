#pragma once
#include <Arduino.h>

// SD-card storage for songs. The card sits on VSPI (config.h PIN_SD_*), a
// different bus from the LCD, so mounting it never disturbs the display.
// Everything degrades gracefully: with no card, sd_available() is false and
// the Songs screen simply says so.

bool     sd_begin();                                  // mount + ensure /midi exists
bool     sd_available();
int      sd_listSongs(String* out, int maxNames);     // .mid basenames in /midi
bool     sd_deleteSong(const char* basename);
String   sd_songPath(const char* basename);           // "/midi/<name>.mid"
uint64_t sd_freeKB();                                  // free space, KiB (0 if none)
