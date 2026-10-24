# CYD BLE Bot Scan

Bluetooth awareness scanner for the ESP32 "Cheap Yellow Display" (CYD).
Scans BLE advertisements and Classic BR/EDR inquiries, names what it can from the
advertised name, the IEEE OUI registry and the Bluetooth SIG company identifiers,
and shows a live, touch-driven list of everything nearby. Optionally logs every new
device to the SD card.

> **Documentation lives in three files:**
> - **[FEATURES.md](FEATURES.md)** — what it does, how the scanning and naming
>   pipeline works, screens, serial protocol, SD logging, build and flash workflow.
> - **[TODO.md](TODO.md)** — what is next, ideas, known issues, and what is done.
> - **README.md** (this file) — the short version.

---

## Status

Working: BLE + Classic scanning, device naming (OUI / company id / address type),
list and detail screens, JSON capture over serial, SD-card logging.

Working: SD-card logging *and* touch, simultaneously — display + SD on VSPI, touch on
HSPI. Each new device is appended to `/ble.jsonl` on the card, so the device can build
a corpus unattended. See FEATURES.md for the bus arrangement and the rules that keep it
working.

## What it does

- Active BLE scan (names included — the scan response is read, not dropped)
- Classic BR/EDR inquiry in dual-mode, 10 s every 60 s
- Identifies devices by: advertised name, OUI vendor, SIG company id, address type
- Live list with RSSI bars, sort modes, shield/alert mode, swipe paging, and a detail screen on tap
- Logs one JSON line per newly seen device to serial and to `/ble.jsonl` on the SD card

## Screens

| Screen | How to get there |
|---|---|
| List | boot |
| Detail | tap any row; tap again to close |

## Build

```bash
pio run -e cyd_ble_bot_scan_2usb     # 2-USB revision (flipped panel)
pio run -e cyd_ble_bot_scan          # 1-USB revision
```

The post-build script merges bootloader + partition table + app into
`CYD-BLE-BOT-SCAN-<usb>.bin` in the project root. Copy that file to the SD card and
flash it from the launcher on the device.

## Serial (115200)

`R` ready · `S` screenshot (RGB332) · `J` toggle JSON capture · `L` toggle SD logging ·
`M`/`T` panel/touch rotation (2-USB build only)