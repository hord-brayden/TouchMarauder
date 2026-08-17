#include "sdstore.h"
#include "config.h"
#include <SPI.h>
#include <SD.h>
#include <FS.h>

static SPIClass s_spi(VSPI);
static bool     s_ok = false;

bool sd_begin() {
  s_spi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  // 20 MHz is conservative and reliable on the CYD's shared 3.3V rail.
  s_ok = SD.begin(PIN_SD_CS, s_spi, 20000000);
  if (!s_ok) { Serial.println("[sd] no card / mount failed"); return false; }
  if (!SD.exists(SD_DIR_SONGS)) SD.mkdir(SD_DIR_SONGS);
  Serial.printf("[sd] mounted, %llu KiB free\n", sd_freeKB());
  return true;
}

bool sd_available() { return s_ok; }

String sd_songPath(const char* basename) {
  String n(basename);
  if (!n.endsWith(".mid")) n += ".mid";
  return String(SD_DIR_SONGS) + "/" + n;
}

int sd_listSongs(String* out, int maxNames) {
  if (!s_ok) return 0;
  File dir = SD.open(SD_DIR_SONGS);
  if (!dir || !dir.isDirectory()) return 0;
  int n = 0;
  for (File e = dir.openNextFile(); e && n < maxNames; e = dir.openNextFile()) {
    String name = e.name();
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);   // some cores return full paths
    if (!e.isDirectory() && name.endsWith(".mid")) {
      out[n++] = name.substring(0, name.length() - 4);  // strip ".mid"
    }
    e.close();
  }
  dir.close();
  return n;
}

bool sd_deleteSong(const char* basename) {
  if (!s_ok) return false;
  return SD.remove(sd_songPath(basename));
}

uint64_t sd_freeKB() {
  if (!s_ok) return 0;
  return (SD.totalBytes() - SD.usedBytes()) / 1024ULL;
}
