#!/usr/bin/env python3
"""
TouchBoard USB-serial MIDI bridge.

The CYD's ESP32 is a classic chip with no native USB, so its "USB MIDI" is
raw MIDI bytes streamed over the CH340 serial port. This script reads that
stream and re-emits it on a virtual MIDI port your DAW can select, exactly
like a hardware controller.

    macOS / Linux / Windows

Setup (once):
    python3 -m pip install pyserial python-rtmidi

Run:
    python3 midi_serial_bridge.py                 # auto-detects the CH340 port
    python3 midi_serial_bridge.py --port /dev/cu.usbserial-210
    python3 midi_serial_bridge.py --list          # list serial ports and exit

Then in your DAW, choose the MIDI input named "TouchBoard USB".
On the board: MIDI view -> SET -> toggle "USB: ON" (that also mutes the
firmware's debug log so it can't corrupt the byte stream).

Note: keep only ONE thing talking to the serial port. Close the Arduino
Serial Monitor before running this, or the port will be busy.
"""
import argparse
import sys
import time

BAUD = 115200
VIRTUAL_PORT_NAME = "TouchBoard USB"


def find_port():
    from serial.tools import list_ports
    candidates = []
    for p in list_ports.comports():
        hay = f"{p.device} {p.description} {p.manufacturer or ''}".lower()
        if any(k in hay for k in ("usbserial", "wch", "ch340", "ch910", "slab", "cp210", "ttyusb")):
            candidates.append(p.device)
    return candidates[0] if candidates else None


def list_ports_and_exit():
    from serial.tools import list_ports
    ports = list(list_ports.comports())
    if not ports:
        print("No serial ports found.")
    for p in ports:
        print(f"  {p.device:24} {p.description}")
    sys.exit(0)


def msg_len(status):
    """Bytes of data that follow a status byte (excluding the status)."""
    hi = status & 0xF0
    if hi in (0xC0, 0xD0):
        return 1
    if hi == 0xF0:                      # system common / realtime
        return {0xF1: 1, 0xF2: 2, 0xF3: 1}.get(status, 0)
    return 2                            # note on/off, poly AT, CC, pitch bend


def main():
    ap = argparse.ArgumentParser(description="Bridge TouchBoard serial MIDI to a virtual MIDI port.")
    ap.add_argument("--port", help="serial device (default: auto-detect)")
    ap.add_argument("--baud", type=int, default=BAUD)
    ap.add_argument("--list", action="store_true", help="list serial ports and exit")
    args = ap.parse_args()

    try:
        import serial
        import rtmidi
    except ImportError:
        sys.exit("Missing deps. Run: python3 -m pip install pyserial python-rtmidi")

    if args.list:
        list_ports_and_exit()

    port = args.port or find_port()
    if not port:
        sys.exit("No CH340-style serial port found. Use --list to see options, then --port.")

    midi_out = rtmidi.MidiOut()
    midi_out.open_virtual_port(VIRTUAL_PORT_NAME)
    print(f"virtual MIDI port open: '{VIRTUAL_PORT_NAME}'")

    while True:
        try:
            ser = serial.Serial(port, args.baud, timeout=0.05)
            print(f"listening on {port} @ {args.baud}  (Ctrl-C to quit)")
        except serial.SerialException as e:
            print(f"open {port} failed ({e}); retrying in 2s")
            time.sleep(2)
            continue

        status = 0          # running status
        data = []
        need = 0
        try:
            while True:
                chunk = ser.read(64)
                for b in chunk:
                    if b >= 0xF8:                       # realtime: pass straight through
                        midi_out.send_message([b])
                        continue
                    if b & 0x80:                        # new status byte
                        status = b
                        data = []
                        need = msg_len(status)
                        if need == 0:                   # zero-data status (e.g. tune request)
                            midi_out.send_message([status])
                            status = 0
                        continue
                    if status == 0:                     # stray data byte, no status yet
                        continue
                    data.append(b)                      # data byte (running status supported)
                    if len(data) >= need:
                        midi_out.send_message([status] + data)
                        data = []                       # keep `status` for running status
        except KeyboardInterrupt:
            print("\nbye")
            ser.close()
            return
        except serial.SerialException:
            print("serial dropped; reconnecting")
            try:
                ser.close()
            except Exception:
                pass
            time.sleep(1)


if __name__ == "__main__":
    main()
