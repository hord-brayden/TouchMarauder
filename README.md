# TouchMarauder

Two completely different firmwares living on one $10 board, and you flip between
them with a tap. No recompiling, no reflashing, no picking a lane.

One **Cheap Yellow Display** (ESP32-2432S024C, 2.4" capacitive) runs:

- **Marauder** — plain-jane [ESP32 Marauder](https://github.com/justcallmekoko/ESP32Marauder),
  the WiFi/Bluetooth investigation toolkit. RSSI hunting, wardriving, packet
  capture, BLE analyzer, the whole "look how cool an ESP32 is" party trick.
- **TouchBoard** — a full touchscreen Bluetooth keyboard I built from scratch.
  Pairs with your phone/laptop/TV as a real HID keyboard: T9, a numpad, arrow
  keys, and a landscape QWERTY. It also has a full **MIDI controller** mode —
  keys, drum pads, CC faders, an on-board sequencer, and SD-card song
  record/save/load — over BLE-MIDI or a wired USB-serial bridge. See
  [`TouchBoard/HOW_TO_PLAY.md`](TouchBoard/HOW_TO_PLAY.md).

Both are wedged into the ESP32's two OTA app slots. Marauder gets **TouchBoard**
and **MIDI** tiles in its main menu (the MIDI tile drops you straight into the
controller); TouchBoard gets an **Exit to Marauder** button on its BT tab. Tap
one, the board reboots into the other in about a second. That's the whole trick —
two apps, one flash chip, a friendly bootloader shove between them.

## How the switch works

The CYD's 4MB flash is carved into two 1920KB app slots (`ota_0`, `ota_1`):

```
  app0 / ota_0  ->  Marauder  ("Brayden's Tubular Marauder + TouchBoard Mod", v1.1)
  app1 / ota_1  ->  TouchBoard
```

Each app calls `esp_ota_set_boot_partition()` on the other slot and reboots.
Nothing is copied or erased on a switch — both firmwares just sit there,
resident, waiting their turn.

One gotcha worth knowing (it cost me an afternoon): both apps use NimBLE, and
they were fighting over the same NVS bond namespace — Marauder's older NimBLE
would read TouchBoard's newer bond format and smash its own stack into a boot
loop. Fix: Marauder's bond store lives under its own namespace now. They keep
their Bluetooth to themselves like well-adjusted roommates.

## Flash a blank CYD

You need the [capacitive 2.4"](https://www.espboards.dev/esp32/cyd-esp32-2432s024/)
variant (CST820 touch, ILI9341 display). Prebuilt binaries are in `firmware/`;
full offsets + the one-liner are in [`firmware/FLASH.md`](firmware/FLASH.md).

Short version, with the board on USB:

```sh
python3 -m esptool --chip esp32 -p /dev/cu.usbserial-XXXX -b 460800 write_flash \
  0x1000   firmware/bootloader.bin \
  0x8000   firmware/partitions.bin \
  0xe000   firmware/boot_app0.bin \
  0x10000  firmware/marauder_v1.1_touchboard-mod.bin \
  0x1f0000 firmware/touchboard.bin
```

It boots into Marauder. Main menu → **TouchBoard** to cross over.

## Build from source

- **TouchBoard** → [`TouchBoard/`](TouchBoard/) — Arduino, esp32 core 3.x,
  LovyanGFX + NimBLE 2.x. See its [README](TouchBoard/README.md).
- **Marauder mod** → [`marauder-mod/`](marauder-mod/) — it's a small patch on
  top of Fr4nkFletcher's CYD fork, not a whole copy of their tree. Build recipe,
  the exact diff, and the "why is it on fire" notes are in
  [`marauder-mod/BUILD_NOTES.md`](marauder-mod/BUILD_NOTES.md).

## Hardware

- **ESP32-2432S024C** — 2.4" CYD, **capacitive** (CST820) touch, ILI9341, no PSRAM.
- The resistive `...024R` variant will **not** work with this touch code.
- USB-C for power + flashing. No battery on this board (it's a mod-your-own pad).

## Credits

- **[justcallmekoko](https://github.com/justcallmekoko/ESP32Marauder)** — the
  original ESP32 Marauder. None of the Marauder half exists without them.
- **[Fr4nkFletcher](https://github.com/Fr4nkFletcher/ESP32-Marauder-Cheap-Yellow-Display)**
  — the CYD port this mod patches, including capacitive 2.4" support.
- TouchBoard is mine.

## Licensing

- **TouchBoard** and this repo's own glue: **MIT** (see [`LICENSE`](LICENSE)).
- The **Marauder mod is GPL-3.0**, because Marauder is — see
  [`marauder-mod/LICENSE`](marauder-mod/LICENSE) and the credits above. The
  `marauder-mod/` folder and the Marauder binary in `firmware/` fall under GPL-3.0.

## One serious note

Marauder ships real transmit tools — deauth, beacon spam, evil portal. Great for
showing off on **your own** gear and networks. Pointing them at networks or
devices you don't own is illegal basically everywhere, so don't. Have fun, be
the person who knows how WiFi works, not the person explaining it to a judge.
