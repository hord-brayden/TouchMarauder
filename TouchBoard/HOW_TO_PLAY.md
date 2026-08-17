# Playing with TouchBoard MIDI

**The one thing to know:** the board *sends* MIDI — it doesn't make sound on its
own. Your computer or phone turns those notes into audio using a synth app
(GarageBand, Ableton, any synth). So: **board → phone/computer → synth → speakers.**

Open the **MIDI** mode first: from Marauder tap the **MIDI** tile, or tap
**TouchBoard** then the **MIDI** tab. (It only shows up for pairing while this
mode is open.)

---

## Make a sound in 60 seconds (Mac, wireless)

1. On the board, be in **MIDI** mode.
2. Mac: open **Audio MIDI Setup** → menu **Window ▸ Show MIDI Studio** → click the
   **Bluetooth** icon → find **TouchBoard MIDI** → **Connect**.
3. Open **GarageBand** → new **Software Instrument** track → pick any sound.
4. Tap the **KEYS** on the board. You should hear it. 🎹

That's it. Same idea on **iPhone/iPad**: GarageBand → gear icon →
**Advanced ▸ Bluetooth MIDI Devices** → connect **TouchBoard MIDI**.

---

## Wired instead (lowest latency)

1. Plug the board into the computer's USB.
2. On the board: **SET** tab → tap **USB** so it says **USB: ON**.
3. On the computer, once: `pip install pyserial python-rtmidi`
4. Run it: `python3 tools/midi_serial_bridge.py`
   (creates a MIDI port called **TouchBoard USB**)
5. In your synth/DAW, choose **TouchBoard USB** as the MIDI input.

Close the Arduino Serial Monitor first — only one program can use the USB port.

---

## The six screens

| Tab | What you do |
|-----|-------------|
| **KEYS** | Play notes. **–/+** change octave. The slider sets how loud. Slide your finger across keys to glide. |
| **PADS** | 16 drum pads (kick, snare, hats…). |
| **CC** | Sliders for Mod / Volume / Pan / filter Cutoff, a **SUS** (sustain) button, and a pitch-bend bar that snaps back when you let go. |
| **REC** | Record button, Play, Stop, **Loop**, **Metro**nome, tempo (**BPM**), and **Tr–/Tr+** (shift pitch) / **Quantize** to tidy timing. **SAVE TAKE** writes it to the SD card. |
| **SONG** | Your saved songs on the SD card — pick one, **LOAD** it, **DELETE**, or scroll with **Up/Dn**. |
| **SET** | MIDI channel, octave, velocity, turn **BLE/USB** on or off, and **PANIC** if a note ever gets stuck. |

---

## Record a little loop

1. **REC** tab → tap **REC**, play a few notes, tap **STOP**.
2. Tap **PLAY** to hear it back. Turn on **LOOP** to make it repeat.
3. To layer more on top: with a recording already there, tap **REC** again and
   play along — your first part keeps playing underneath (that's how you build
   chords, since you can only press one key at a time).
4. Tap **SAVE TAKE** — it lands on the SD card as `take-001.mid`, `take-002.mid`, …
   Find it later on the **SONG** tab, or drag it off the SD card into any DAW.

---

## If it's not working

- **Notes show up but no sound** → your synth isn't listening. Make sure a
  software-instrument track is selected/armed, and that it's using the TouchBoard
  MIDI input.
- **Nothing happens at all** → you're probably not in **MIDI** mode on the board
  (it only advertises MIDI there), or you paired while it was still in keyboard
  mode. Re-open MIDI mode and reconnect.
- **A note is stuck on** → **SET** tab → **PANIC**.
- **Reconnecting after a while** → the first connection on a new machine is a
  fresh Bluetooth pair; that's normal.
