#include <Arduino.h>
#include <vector>
#include "smf.h"
#include "config.h"

// ---- little helpers ----------------------------------------------------
static void put32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x);
}
static void put16(std::vector<uint8_t>& v, uint16_t x) {
  v.push_back(x >> 8); v.push_back(x);
}
// MIDI variable-length quantity (7 bits/byte, high bit = "more follows").
static void putVar(std::vector<uint8_t>& v, uint32_t value) {
  uint8_t buf[5]; int n = 0;
  buf[n++] = value & 0x7F;
  while ((value >>= 7)) buf[n++] = (value & 0x7F) | 0x80;
  while (n) v.push_back(buf[--n]);
}

static uint32_t msToTicks(uint32_t ms, int bpm) {
  // ticks = ms * PPQ * bpm / 60000
  return (uint32_t)((uint64_t)ms * MIDI_SMF_PPQ * bpm / 60000ULL);
}

bool smf_write(fs::File& f, const MidiEvent* ev, int n, int bpm) {
  if (bpm <= 0) bpm = MIDI_DEFAULT_BPM;

  // Header chunk.
  std::vector<uint8_t> hdr;
  hdr.insert(hdr.end(), {'M','T','h','d'});
  put32(hdr, 6);
  put16(hdr, 0);                 // format 0
  put16(hdr, 1);                 // one track
  put16(hdr, MIDI_SMF_PPQ);

  // Track data.
  std::vector<uint8_t> trk;
  // tempo meta at t=0: FF 51 03 <us-per-quarter>
  uint32_t usPerQuarter = 60000000UL / bpm;
  putVar(trk, 0);
  trk.insert(trk.end(), {0xFF, 0x51, 0x03});
  trk.push_back(usPerQuarter >> 16); trk.push_back(usPerQuarter >> 8); trk.push_back(usPerQuarter);

  uint32_t prevTick = 0;
  for (int i = 0; i < n; i++) {
    uint32_t tick = msToTicks(ev[i].tMs, bpm);
    uint32_t delta = tick >= prevTick ? tick - prevTick : 0;
    prevTick = tick;
    putVar(trk, delta);
    uint8_t len = midi_msgLen(ev[i].status);
    trk.push_back(ev[i].status);
    trk.push_back(ev[i].d1);
    if (len == 3) trk.push_back(ev[i].d2);
  }
  putVar(trk, 0);
  trk.insert(trk.end(), {0xFF, 0x2F, 0x00});   // end of track

  std::vector<uint8_t> trkHdr;
  trkHdr.insert(trkHdr.end(), {'M','T','r','k'});
  put32(trkHdr, trk.size());

  if (f.write(hdr.data(), hdr.size()) != hdr.size()) return false;
  if (f.write(trkHdr.data(), trkHdr.size()) != trkHdr.size()) return false;
  if (f.write(trk.data(), trk.size()) != trk.size()) return false;
  return true;
}

// ---- reader ------------------------------------------------------------
static uint32_t readVar(fs::File& f, bool& ok) {
  uint32_t value = 0; int guard = 0;
  while (guard++ < 5) {
    int c = f.read();
    if (c < 0) { ok = false; return 0; }
    value = (value << 7) | (c & 0x7F);
    if (!(c & 0x80)) { ok = true; return value; }
  }
  ok = false; return 0;
}

int smf_read(fs::File& f, MidiEvent* ev, int maxEvents, int& bpmOut) {
  bpmOut = MIDI_DEFAULT_BPM;
  uint8_t magic[4];
  if (f.read(magic, 4) != 4 || memcmp(magic, "MThd", 4) != 0) return -1;

  // Layout after magic: length(4) then `length` bytes = format(2) ntracks(2)
  // division(2). length is normally 6 but the spec allows more; honor it so the
  // first MTrk is located correctly.
  uint8_t lenb[4];
  if (f.read(lenb, 4) != 4) return -1;
  uint32_t hdrLen = ((uint32_t)lenb[0] << 24) | (lenb[1] << 16) | (lenb[2] << 8) | lenb[3];
  uint8_t hb[6];
  if (f.read(hb, 6) != 6) return -1;           // format(2) ntracks(2) division(2)
  uint16_t division = (hb[4] << 8) | hb[5];
  if (hdrLen != 6) f.seek(8 + hdrLen);         // skip any extended header to the first chunk
  if (division == 0) division = MIDI_SMF_PPQ;
  int ppq = (division & 0x8000) ? MIDI_SMF_PPQ : division;  // SMPTE division unsupported -> default

  // Find the first MTrk.
  while (true) {
    uint8_t tm[4];
    if (f.read(tm, 4) != 4) return 0;
    uint8_t lb[4];
    if (f.read(lb, 4) != 4) return 0;
    uint32_t tlen = ((uint32_t)lb[0] << 24) | (lb[1] << 16) | (lb[2] << 8) | lb[3];
    if (memcmp(tm, "MTrk", 4) == 0) {
      // parse this track
      long trackEnd = f.position() + tlen;
      uint32_t curBpm = MIDI_DEFAULT_BPM;
      double msPerTick = 60000.0 / (curBpm * ppq);
      double absMs = 0;
      uint8_t running = 0;
      int count = 0;
      bool firstTempo = true;

      while (f.position() < trackEnd) {
        bool ok;
        uint32_t delta = readVar(f, ok);
        if (!ok) break;
        absMs += delta * msPerTick;

        int c = f.read();
        if (c < 0) break;
        uint8_t status = c;
        if (status < 0x80) { status = running; f.seek(f.position() - 1); }  // running status
        else if (status < 0xF0) running = status;

        uint8_t hi = status & 0xF0;
        if (status == 0xFF) {                    // meta
          int type = f.read();
          bool ok2; uint32_t mlen = readVar(f, ok2);
          if (!ok2) break;
          if (type == 0x51 && mlen == 3) {       // tempo
            int t0 = f.read(), t1 = f.read(), t2 = f.read();
            uint32_t us = ((uint32_t)t0 << 16) | (t1 << 8) | t2;
            curBpm = us ? (uint32_t)(60000000UL / us) : MIDI_DEFAULT_BPM;
            msPerTick = 60000.0 / (curBpm * ppq);
            if (firstTempo) { bpmOut = curBpm; firstTempo = false; }
          } else {
            for (uint32_t i = 0; i < mlen; i++) if (f.read() < 0) break;
          }
        } else if (status == 0xF0 || status == 0xF7) {  // sysex: skip
          bool ok2; uint32_t slen = readVar(f, ok2);
          if (!ok2) break;
          for (uint32_t i = 0; i < slen; i++) if (f.read() < 0) break;
        } else if (hi >= 0x80 && hi <= 0xE0) {   // channel voice
          uint8_t len = midi_msgLen(status);
          int d1 = f.read();
          int d2 = (len == 3) ? f.read() : 0;
          if (d1 < 0) break;
          if (count < maxEvents) {
            ev[count].tMs    = (uint32_t)(absMs + 0.5);
            ev[count].status = status;
            ev[count].d1     = (uint8_t)d1;
            ev[count].d2     = (uint8_t)(d2 < 0 ? 0 : d2);
            count++;
          }
        } else {
          break;  // unknown; bail to avoid desync
        }
      }
      return count;
    } else {
      // skip a non-MTrk chunk
      for (uint32_t i = 0; i < tlen; i++) if (f.read() < 0) return 0;
    }
  }
}
