# The Marauder half

This is **not** a copy of ESP32 Marauder. It's a handful of edits on top of
[Fr4nkFletcher's CYD fork](https://github.com/Fr4nkFletcher/ESP32-Marauder-Cheap-Yellow-Display)
(itself a fork of [justcallmekoko's Marauder](https://github.com/justcallmekoko/ESP32Marauder)),
captured as [`marauder-mod.patch`](marauder-mod.patch). Everything here is GPL-3.0
(see [`LICENSE`](LICENSE)) because Marauder is.

## What the patch changes

- **Board:** `CYD_28` → `CYD_24CAP` — the 2.4" capacitive panel.
- **Version + branding:** `v1.4.3` → `v1.1`, and the boot screen + serial banner
  now say *"Brayden's Tubular Marauder + TouchBoard Mod"* instead of the stock
  credit. (Wardriving CSVs keep `brand=JustCallMeKoko` so wigle.net uploads still
  parse — that's a data field, not vanity.)
- **New main-menu tile: `TouchBoard`** — sets the boot partition to `ota_1` and
  reboots into the keyboard.
- **New main-menu tile: `MIDI`** — same jump to `ota_1`, but first drops a
  one-shot flag in the shared `touchboard` NVS namespace so TouchBoard opens
  straight on its MIDI controller view.
- **GPS off** — no GPS module on this board, and GPS init hung the boot. This is
  effectively the `nogps` build.
- **NimBLE bond namespace** → `mrdr_bond` so Marauder and TouchBoard stop
  clobbering each other's Bluetooth keys in NVS (that was the boot-loop). This
  one lives in a *separate* patch, [`nimble-bond-namespace.patch`](nimble-bond-namespace.patch),
  because it edits the bundled NimBLE library rather than the sketch — apply it
  too or the rebuild boot-loops the moment TouchBoard has bonded a host.

## Building it (the part that fights back)

Marauder v1.4.3 was written against the **old ESP32 core (2.0.x)** — it uses WiFi
internals that core 3.x deleted. So:

- **esp32 core `2.0.17`** (not 3.x, and not "2.0.18" — that only exists in
  Arduino's Nano fork). Install it isolated if you don't want to downgrade your
  main setup: `ARDUINO_DIRECTORIES_DATA=/some/scratch arduino-cli core install esp32:esp32@2.0.17`.
- **Libraries:** the ones bundled in the upstream fork's `/libraries`. The one
  that matters most is TFT_eSPI — use the **`2.4C`** variant from `TFT_eSPI-CYD/`,
  not the parent folder (it ships a dozen board-specific TFT_eSPI copies and the
  compiler will grab the wrong one). Also needed: `bb_captouch`, `SensorLib`,
  `lv_arduino`, `NimBLE-Arduino` (1.3.5), `LinkedList`, `ArduinoJson`,
  `JPEGDecoder`, `Adafruit_NeoPixel`, `AsyncTCP`, `ESPAsyncWebServer`,
  `MicroNMEA`, `EspSoftwareSerial`, `Adafruit_MAX1704X`, `Adafruit_BusIO`.
- **FQBN:** `esp32:esp32:esp32:PartitionScheme=huge_app,FlashSize=4M,PSRAM=disabled,CPUFreq=240,FlashFreq=80,FlashMode=dio`
- **One extra link flag:** `compiler.c.elf.extra_flags=-Wl,--allow-multiple-definition`.
  Marauder defines a pile of globals and helper functions right in its headers,
  which the newer linker rightly hates. The definitions are identical across
  every translation unit, so "keep the first" is safe here.

Roughly:

```sh
git clone https://github.com/Fr4nkFletcher/ESP32-Marauder-Cheap-Yellow-Display
cd ESP32-Marauder-Cheap-Yellow-Display
git apply /path/to/marauder-mod.patch
git apply /path/to/nimble-bond-namespace.patch   # bond-namespace fix (required)
# point --libraries at the bundled libs (with the 2.4C TFT_eSPI as "TFT_eSPI")
arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app,FlashSize=4M,PSRAM=disabled" \
  --libraries ./build_libs \
  --build-property "compiler.c.elf.extra_flags=-Wl,--allow-multiple-definition" \
  esp32_marauder
```

## Flashing just this half

The prebuilt binary is `../firmware/marauder_v1.1_touchboard-mod.bin`. It goes in
**app0**, and you flash *only* that region so TouchBoard in app1 survives:

```sh
python3 -m esptool --chip esp32 -p /dev/cu.usbserial-XXXX -b 460800 \
  write_flash --flash_size keep 0x10000 ../firmware/marauder_v1.1_touchboard-mod.bin
```

Full blank-board procedure (both apps + partition table): see
[`../firmware/FLASH.md`](../firmware/FLASH.md).

## If it boot-loops

- **Stack smash right after `Sensor type = CST820`** → NVS bond collision. Either
  you skipped `nimble-bond-namespace.patch`, or there's a stale bond from an
  older build in NVS. Wipe it: `esptool erase_region 0x9000 0x5000` then
  `erase_region 0xe000 0x2000`.
- **Hangs after the battery check** → GPS is on. Turn `HAS_GPS` off in `configs.h`.
- **Wrong colors / garbage screen** → wrong TFT_eSPI variant. Use `2.4C`.
