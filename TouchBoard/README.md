# TouchBoard

A BLE HID keyboard built on the **ESP32-2432S024C** (2.4" capacitive-touch
"Cheap Yellow Display"). Pairs with phones, laptops, tablets, and TVs as a
real hardware keyboard. Five views, switched by the tab bar at the top:

| Tab | View |
|-----|------|
| `T9`  | Nokia-style multi-tap keypad (see below) with tall Backspace/Enter |
| `123` | Numpad with operators (`/ * - + = %`), Tab, Enter |
| `nav` | Arrows, Home/End/PgUp/PgDn, Esc, Tab, Del, Copy/Cut/Paste |
| `QWE` | Full **landscape** QWERTY for when you actually have a sentence to type |
| `BT`  | Connection status, paired-host count, re-advertise / forget / **Exit to Marauder** |
| `MIDI`| A full MIDI controller (keys, pads, CC faders, sequencer, SD songs) — see below |

Ships as the second half of [TouchMarauder](../README.md): the same CYD dual-boots
this keyboard and a customized ESP32 Marauder, and each hands off to the other
with a tap. The **Exit to Marauder** button on the `BT` tab is that hand-off.

## MIDI controller (the `MIDI` tab)

A near-complete MIDI controller runs natively in TouchBoard. Tap the `MIDI` tab
(or, from Marauder, the **MIDI** main-menu tile, which boots straight into it).
Six sub-sections along the top:

| Section | What it does |
|---------|--------------|
| `KEYS`  | One-octave piano, octave −/+, velocity slider. Slide to glissando. |
| `PADS`  | 4×4 MPC-style drum pads (GM drums on channel 10). |
| `CC`    | Faders for Mod / Volume / Pan / Cutoff, a sustain toggle, and a pitch-bend strip (springs back to center on release). |
| `REC`   | Transport: record / play / stop, loop, metronome, BPM, clear, and **Save take** to SD. |
| `SONG`  | Browse `.mid` files on the SD card: load, delete, refresh. |
| `SET`   | MIDI channel, base octave, velocity, transport toggles (BLE / USB), and **Panic** (all-notes-off). |

### Two ways to reach a computer (this board has **no native USB MIDI**)

The classic ESP32 has no USB device peripheral — the USB port is a CH340 serial
bridge for flashing — so class-compliant USB MIDI is physically impossible here.
Instead:

- **BLE-MIDI (wireless).** On macOS: *Audio MIDI Setup → window “MIDI Studio” →
  Bluetooth → connect **TouchBoard MIDI***. Works with Logic, Ableton,
  GarageBand, any DAW. This is the default; toggle it in `SET`.
- **USB-serial (wired, lowest latency).** Enable **USB: ON** in `SET`, then on
  the Mac run [`tools/midi_serial_bridge.py`](tools/midi_serial_bridge.py)
  (`pip install pyserial python-rtmidi`), which exposes a virtual MIDI port
  named *TouchBoard USB*. Enabling USB also mutes the firmware’s serial debug
  log so it can’t corrupt the MIDI byte stream (they share UART0).

Both can be on at once.

### What the hardware can and can’t do

- **Monophonic keys.** The capacitive panel is single-touch, so you can’t hold a
  chord on the piano. Build chords by **overdub**: record a part on `REC`, then
  hit record again and play more on top — the first take plays back underneath.
- **No key-pressure velocity.** Velocity comes from the `KEYS` velocity slider
  (or the `SET` default), not from how hard you press.
- **Songs are Standard MIDI Files** (format 0) on the SD card under `/midi`.
  Saved takes are `take-001.mid`, `take-002.mid`, … . Loading a DAW-exported
  `.mid` works too. “Remix” lives on the sequencer: transpose, quantize, and a
  tempo control that actually re-times the take.

Marauder is never affected — MIDI lives entirely in the TouchBoard app slot.

## T9 typing (traditional buffered multi-tap)

Portrait orientation, phone-style: 3 columns of 80x51px keys, five rows
(`1-9`, `* 0 #`, then `Bksp / > / Enter`), with a preview strip above the
keypad — that strip is the "Nokia screen".

Taps edit a LOCAL candidate buffer shown in the strip; **nothing is sent over
BLE until the character commits**. One clean HID character per commit — no
backspace churn, no dropped BLE notifications mid-cycle.

- **Tap a key** to buffer its first letter; tap again within 1.1s to cycle
  (`2`: a → b → c → 2). The candidate shows in the preview strip.
- The character **commits** when: the timeout expires, you press a *different*
  key (interrupt commit), you press **`>`** (manual advance — how you type
  same-key pairs like "ab" without waiting), Enter, or you switch tabs.
- **Backspace** first deletes the *uncommitted* candidate; with an empty
  buffer it sends a real backspace (hold = host auto-repeat).
- **Digits**: each key's cycle ends in its digit (tap `2` four times -> `2`),
  or use the `123` numpad tab. (There is no long-press shortcut: this panel
  reports "finger down" past any sane long-press threshold after a physical
  lift, so long-press fired on every tap and turned letters into digits.)
- **`*` cycles case**: `abc → Abc → ABC` (one-shot shift, then caps lock).
- **`1`** = punctuation (`. , ? ! ' "`), **`#`** = symbols (`@ - _ / :`),
  **`0`** = space (second tap = the digit 0).

Timings: `T9_MULTITAP_MS` / `T9_LONGPRESS_MS` / `T9_MIN_TAP_MS` in `config.h`.

## QWERTY (the `QWE` tab)

T9 is charming. T9 is also slow, and after tapping `7` four times to get an `s`
for the hundredth time you will want a real keyboard. So the `QWE` tab is one:
a full landscape QWERTY, the only screen that rotates the panel sideways
(320x240) and rotates it right back when you leave.

- Tap **shift** once = next letter capital (one-shot). Tap it again = **CAPS
  LOCK** (the key lights up and reads `CAPS`). Tap a third time = back to normal.
- **abc** (bottom-left) drops you back to portrait T9.
- Everything types straight over BLE, no candidate buffer — it's a keyboard,
  not a T9 puzzle.

The landscape touch mapping is derived from the portrait one in `ui.cpp`
(`qwertyMap`); if a future board reports touch differently, that's the one
function to poke.

## Ghost-touch defense (three layers)

1. **INT gating** — the CST820 pulses its INT line (GPIO 21) for genuine
   reports; finger-down register data with no recent pulse is discarded.
2. **Frame filter** — 3 consecutive in-range, non-jumping frames to press,
   3 empty frames to release (`touch.cpp`).
3. **Tap semantics** — T9 keys fire on *release* and only if the contact
   lasted >= `T9_MIN_TAP_MS`; a one-frame phantom can't type.

If ghosts persist through all three layers, suspect the power supply: these
panels float their capacitance reference on dirty USB power. Test on a
laptop port before blaming firmware.

## A BLE HID timing gotcha (why T9 letters once "didn't type")

A tapped key must send its key-DOWN and key-UP HID reports in *separate* BLE
connection events. Fired back-to-back (microseconds apart) the host coalesces
them and registers no press — the report is correct, it's just too fast.
`hidkb_tap()` holds the key for `HID_TAP_HOLD_MS` (config.h) between the two.
Keys that send down-on-touch / up-on-release (numpad, Backspace, Enter) never
hit this because your finger supplies the gap.

Onboard RGB LED: **blinking blue** = advertising (discoverable), **green** = connected.

## Build setup

Tested with **esp32 core 3.3.10** (already installed on this machine).

Libraries (already cloned into `~/Documents/Arduino/libraries/`):
- **NimBLE-Arduino 2.5.0** — the HID code uses the NimBLE **2.x** API
  (`getInputReport`, `setReportMap`, ...). It will NOT compile against 1.x.
  Don't downgrade: 2.3.4 compiled fine but boot-looped on esp32 core 3.3.10
  with `assert failed: npl_freertos_mutex_pend` inside `createServer()` —
  the library and core versions must be recent *together*.
- **LovyanGFX 1.2.7** — display driver, configured entirely in `display.h`.

Arduino IDE settings:
- Board: **ESP32 Dev Module**
- Partition Scheme: **Huge APP (3MB No OTA)** — the sketch currently fits the
  default scheme too, but BLE + graphics grows fast; this buys headroom.
- Everything else: defaults.

Or from the command line:

```sh
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app .
arduino-cli upload  --fqbn esp32:esp32:esp32:PartitionScheme=huge_app -p /dev/cu.usbserial-XXXX .
```

## Pairing — read this first

A BLE keyboard is a *peripheral*: it advertises, and the **host initiates
pairing**. The device cannot reach out and attach itself to your phone —
that's a Bluetooth role limitation, not a firmware gap. So:

1. Power the board (blue blinking LED = it's advertising).
2. On the phone/laptop: Bluetooth settings → pair with **TouchBoard**.
3. Pairing is "Just Works" (no PIN) and bonded — after the first pairing,
   bonded hosts reconnect automatically whenever the board is advertising.

To move it to a different host: `BT` tab → **Forget hosts**, then also
"Forget this device" on the old host, then pair from the new one.

**Phone behavior note:** once paired, Android/iOS treat this as a hardware
keyboard and *hide their on-screen keyboard*. That's correct — this device IS
the keyboard now. Unpair (or just power the board off) to get the soft
keyboard back.

## File map

```
TouchBoard.ino   setup/loop: touch polling, status LED
config.h         all pins, BLE name, touch orientation flags
display.h        LovyanGFX panel config (ILI9341, HSPI, backlight PWM)
touch.h/.cpp     raw-I2C CST820 capacitive touch driver
hidkb.h/.cpp     BLE HID keyboard on NimBLE 2.x (report map, pairing, bonds)
keymap.h         all key layouts as data tables (HID usage IDs + layout units)
ui.h/.cpp        rendering, hit testing, view/layer state machine
```

The flow per keypress: touch down → `ui.cpp` hit-tests the grid → T9 engine
sends tap (or key-DOWN for Backspace/Enter so the host auto-repeats).
Touch frames pass a stability filter in `touch.cpp` (4 consecutive in-range,
non-jumping frames to press; 3 empty frames to release) because the CST820
emits noise frames, especially on dirty USB power.

## Touch calibration

On boot the serial monitor prints the touch chip ID, and every tap logs
`[touch] down at x,y`. If taps land mirrored or rotated, flip
`TOUCH_SWAP_XY` / `TOUCH_INV_X` / `TOUCH_INV_Y` in `config.h` until a tap in
the top-left corner logs near `0,0` and bottom-right logs near `319,239`.

## Known issues / hardware gotchas

- **R vs C variant.** The 2432S024 ships with capacitive (CST820, this code)
  or resistive (XPT2046, different bus, different driver) touch. If serial
  says `no CST8xx touch controller at 0x15`, you have the R variant.
- **Display controller variance.** Most C boards are ILI9341. If colors look
  inverted set `cfg.invert = true`; if red/blue swap set `cfg.rgb_order = true`
  (both in `display.h`). If the image is garbage, the panel may be an ST7789 —
  swap `Panel_ILI9341` for `Panel_ST7789` in the same file.
- **One host at a time.** It bonds to multiple hosts but connects to one.
  Real multi-device keyboards do bond-slot switching — feasible later
  (directed advertising per stored bond) but not implemented.
- **Copy/Cut/Paste keys send Ctrl+C/X/V** — right for Windows/Linux/Android.
  For macOS/iOS change `KMOD_CTRL` to `KMOD_GUI` (Cmd) in `keymap.h`.
- **Single-touch only.** No chording two keys; shift is a sticky one-shot
  key instead, like a phone keyboard.
- **US layout assumption.** HID sends *key positions*, the host applies its
  layout. On a non-US host layout, symbols will come out shifted around.
- **No battery on this board** — it's USB-powered. A LiPo + charge board on
  the exposed pads is the usual mod.
- **Battery service reports a hardcoded 100%.**

## Ideas for later

- Key-click sound (speaker is on GPIO 26) and haptics
- Consumer-control page for media keys (vol/play) — separate HID report
- Macro keys / text snippets view
- Multi-host bond slots with a host-switcher on the BT screen
- Backlight dim on idle (LDR on GPIO 34 could auto-adjust)
