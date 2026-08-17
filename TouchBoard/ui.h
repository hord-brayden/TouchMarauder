#pragma once
#include <stdint.h>

enum ViewId : uint8_t { VIEW_KB = 0, VIEW_NUM, VIEW_NAV, VIEW_QWERTY, VIEW_BT, VIEW_MIDI, VIEW_COUNT };

void ui_begin();
void ui_bootIntoMidi();       // call BEFORE ui_begin() to open on the MIDI view
void ui_onTouchDown(int x, int y);
void ui_onTouchMove(int x, int y);   // finger dragged while still down (MIDI faders/glissando)
void ui_onTouchUp();
void ui_tick(uint32_t now);   // call every loop; refreshes status when BLE state changes
