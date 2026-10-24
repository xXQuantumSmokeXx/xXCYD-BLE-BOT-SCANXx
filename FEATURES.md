# CYD BLE Bot Scan — Features

A standalone Bluetooth awareness scanner for the ESP32 Cheap Yellow Display.
It listens for BLE advertisements (and Classic BR/EDR inquiries when available),
names what it can, shows a live list on the 320x240 touchscreen, and can log every
new device to the SD card.

---

## Hardware

| Item | Detail |
|---|---|
| Board | ESP32 (ESP32-32E, 4 MB flash) — 1-USB and 2-USB revisions |
| Display | ILI9341 320x240, MOSI 13, MISO 12, SCLK 14, CS 15, DC 2, BL 21 |
| Touch | XPT2046, CS 33, IRQ 36, MOSI 32, MISO 39, SCLK 25 |
| SD card | CS 5, shares the display's SCLK/MOSI/MISO |
| Radio | Single 2.4 GHz — BLE and Classic share it; no Wi-Fi in this firmware |

The 2-USB revision has its panel physically flipped, so it needs a different build
(`CYD_USB_VERSION=2`).

### SPI bus topology (important)

Three devices, two buses, no sharing:

```
HSPI (bus 1)  ── touch controller              CS 33, pins 25 / 39 / 32
VSPI (bus 2)  ── display + SD card             CS 15 and CS 5, pins 14 / 12 / 13
```

Rules learned the hard way:

- The core's global `SPI` object is VSPI, and the display uses it.
- **Never call `SPI.begin()` after `tft.init()`.** It re-maps VSPI's pins and takes
  them from anything else on that bus. `SD.begin(SD_CS, SPI, 4 MHz)` on its own does
  not disturb the mapping — that is why the card now mounts.
- The touch controller lives on **HSPI** deliberately, so SD traffic on VSPI cannot
  affect it. Moving the *display* to HSPI instead made the card fail to mount.

---

## Scanning pipeline

```
BLE controller (dual mode BTDM)
   └─ active scan, wantDuplicates=true
        └─ onResult callback  ──►  BLE queue (64 entries)
                                     └─ btScannerUpdate()  ──►  device table (60)
Classic inquiry (every 60 s, 10 s long) ────────────────────────┘
                                     └─ sort / expire / log / redraw
```

- **Active scan with `wantDuplicates=true`.** Required: with the library default the
  scan-response report is discarded as a duplicate, and most devices put their name
  only in the scan response.
- **Dual-mode init.** The controller is brought up as `ESP_BT_MODE_BTDM` *before*
  `BLEDevice::init()`. Doing it afterwards fails with `ESP_ERR_INVALID_STATE`, and
  Classic inquiry never works. Falls back to BLE-only if the pre-init fails.
- **Device table**: 60 entries, 60 s expiry, LRU eviction, deduped by MAC.

---

## Identification layers

Tried in order; later layers name devices the earlier ones cannot.

| Layer | Source | Size | Notes |
|---|---|---|---|
| Advertised name | complete/short local name + manual AD-structure parse | — | Non-ASCII (CJK, emoji) stripped; row then shows the MAC tail with `*` |
| OUI vendor | IEEE OUI registry, generated | 16,327 prefixes / 193 vendors | binary search on the first three octets |
| Company ID | Bluetooth SIG company identifiers, generated | 4,057 ids | from manufacturer data; works with randomised MACs |
| Address type | BLE address type | — | `P` = public/stable, `R` = random/rotating |

Vendor names are normalised at generation time (`Shenzhen DeepX Technology Co., Ltd.`
→ `DeepX`). Regenerate rather than hand-edit:

```bash
node tools/gen-oui.mjs  <oui.csv> [topN]
node tools/gen-cids.mjs <company_identifiers.yaml> [maxId]
```

---

## Detection signatures

Rules live in `signatures.json` and are compiled into `src/modules/signatures.h` by
`tools/gen-signatures.mjs`. A rule matches when every field it specifies matches:

| Field | Meaning |
|---|---|
| `name_prefix` | advertised name starts with this |
| `cid` | SIG company id from manufacturer data |
| `svc16` | a 16-bit service UUID is present |
| `u128` | the captured 128-bit service UUID equals this |
| `vendor_not` | OUI vendor is not this (used to spot Find My accessories) |

First match wins, so specific rules go first. The matched label replaces the device
class in the right-hand column (`P Light`, `R FindMy`), appears as `Match` on the detail
screen, and is logged as `sig` in every JSON line.

Current rules, each backed by corpus evidence:

| Label | Rule |
|---|---|
| `Light` | name starts `Govee_` and company id `0x8803` |
| `Appliance` | name starts `AM410` |
| `Sensor` | 128-bit service `3E1D50CD…` |
| `FindMy` | company id `0x004C` on a non-Apple OUI |

Add a rule by editing `signatures.json` and running the generator; no C++ changes.
## Screens

### List screen

- **Top bar** — `N Devices online`, optional debug counters, power ring (top right)
- **Rows** — bar, RSSI, then `name-or-MAC-tail + vendor`, then `P/R` and device class
  - the whole row is measured and the block centred, so bars sit beside their text
  - green bar = usable signal (>= -70 dBm), red = very weak (< -90) or alert
  - classic devices render in blue
- **Bottom bar** — `ALERT ON/OFF`, sort mode (`RSSI` / `NAME` / ...), `SCAN` / `STOP`
- **Theme dot** — bottom right, cycles the accent colour (persisted)
- **Paging** — swipe up/down scrolls a page (visible rows − 1); a scrollbar and a
  `10-18 / 50` position readout appear when the list is taller than the screen
- **Empty state** — centred "No Devices / Searching..."
- **Boot banner** — `SD OK` (green) or `NO SD CARD` (red) for 1.5 s at startup

### Detail screen (tap a row)

MAC, vendor, address type in words, class, RSSI, TX power, appearance, company id,
source, report count. Any tap closes it.

### Gestures

| Gesture | Where | Action |
|---|---|---|
| Tap a row | list | open the detail screen for that device |
| Tap anywhere | detail | close and return to the list |
| Swipe up | list | next page |
| Swipe down | list | previous page |
| Tap a button | bottom bar | ALERT ON/OFF, sort mode, SCAN/STOP |
| Tap the dot | bottom right | cycle the accent colour |

The swipe detector needs 30 px of travel with a 2:1 axis ratio, so a tap never scrolls.
Paging clamps at both ends, and a row tap after scrolling resolves through the offset.

---

## SD-card logging

Every newly seen device is appended to `/ble.jsonl` in the card root, one JSON object
per line. Code in `src/modules/sd_log.cpp`, enabled with `SD_LOG_ENABLE 1` in
`config.h`.

- Mounts at boot: `SD.begin(SD_CS, SPI, 4 MHz)`, falling back to 1 MHz
- 4 KB RAM buffer; flushed every 2 s or when 75% full (batched to keep bus traffic low)
- `L` toggles logging at runtime; `J` gates capture entirely (log and serial alike)
- Boot banner shows `SD OK` (green) or `NO SD CARD` (red) for 1.5 s

Working configuration: display + SD on VSPI, touch on HSPI — see the bus topology note.
The card is only touched when a flush happens, and the firmware is single-threaded, so a
redraw can never interleave with a write.

The card shares SCLK/MOSI/MISO with the display, so flushes are batched to keep the
number of bus transactions low. Touch is re-asserted after mount and after each flush.

---

## Serial interface (115200)

| Key | Action |
|---|---|
| `R` | print `READY` |
| `S` | screenshot: prints `RGB332:` then 320×240 bytes, one byte per pixel |
| `J` | toggle JSON capture for newly seen devices |
| `H` | toggle raw payload hex dump |
| `L` | toggle SD logging |
| `M` | cycle panel rotation (2-USB build only) |
| `T` | cycle touch rotation (2-USB build only) |

Screenshot decoding: each byte is `RRRGGGBB`; scale R/G by 255/7, B by 255/3.
Byte-at-a-time reads cannot keep up with 115200 — read in chunks.

### JSON format

```json
{"mac":"E8:F6:0A:CB:1C:D1","rssi":-38,"name":"_1cd0","vendor":"Espressif",
 "cid":"0x0000","app":"0x0000","tx":127,"addr":"public","svc":[],
 "v128":true,"u128":"0000FE2C00001000800000805F9B34FB"}
{"src":"classic","mac":"AA:BB:CC:DD:EE:FF","rssi":-61,"name":"X","cod":"0x240404"}
```

| Field | Meaning |
|---|---|
| `mac` | device address |
| `rssi` | dBm at last sighting |
| `name` | advertised local name, printable ASCII only, trimmed |
| `vendor` | OUI lookup, else SIG company id, else empty |
| `cid` | SIG company identifier from manufacturer data (`0x0000` = none) |
| `app` | Bluetooth appearance (`0x0000` = none) |
| `tx` | TX power, `127` = unknown |
| `addr` | `public` (stable) or `random` (rotating) |
| `svc` | 16-bit service UUIDs |
| `v128` / `u128` | first 128-bit service UUID present / its value |
| `cod` | classic only: class of device |

**One line per MAC per boot** — a 128-entry RAM set remembers what was already
written, because entries expire after 60 s and would otherwise re-log. The line is
written after name/vendor are parsed, so it carries the name rather than an empty
string; devices that never advertise a name are written after three sightings.

Serial lines are prefixed with `JSON `; the SD file is bare JSON.

### Raw payload dump

Each newly seen device also emits one hex dump of its advertisement (ADV_IND plus scan
response, up to 62 bytes):

```json
{"raw":"E8:F6:0A:CB:1C:D1","len":31,"hex":"02010606095F31636430..."}
```

Decode it with `node tools/decode-adv.mjs <file>` to see every AD structure, including
malformed ones — the way to check that a device name was parsed from the right bytes.
`H` toggles the dump at runtime.

---

## Build and flash

```bash
pio run -e cyd_ble_bot_scan_2usb        # 2-USB panel
pio run -e cyd_ble_bot_scan             # 1-USB panel
```

Both environments build with `-Wformat=2 -Werror=format`: a format/argument mismatch is a
build error. Format-truncation warnings are treated as errors too, so buffers get sized
rather than silently truncating.

`merge_bin.py` (post-build) concatenates bootloader + partition table + app +
`boot_app0` into `CYD-BLE-BOT-SCAN-<usb>.bin`. That single file is copied to the SD
card and flashed by the launcher on the device.

### Partition table

`partitions.csv` — single 2.94 MB app, no OTA (the device is flashed from SD):

| Name | Offset | Size |
|---|---|---|
| nvs | 0x9000 | 0x5000 |
| otadata | 0xe000 | 0x2000 |
| app0 | 0x10000 | 0x2F0000 |
| spiffs | 0x300000 | 0xF0000 |
| coredump | 0x3F0000 | 0x10000 |

Current usage: ~42.6% of the app partition, ~16.8% of RAM.

---

## Configuration

| Where | What |
|---|---|
| `src/config/config.h` | pins (incl. `SD_CS`), screen geometry, scan intervals, RSSI thresholds, device limits |
| `src/modules/bt_scanner.cpp` | `BLE_QUEUE_SIZE`, `BLE_DEBUG_ADV`, `BT_DUAL_MODE_ATTEMPT` |
| `src/modules/sd_log.cpp` | log path, buffer size, flush interval |
| `src/ui/screens/screen_list.cpp` | `SHOW_ADV_STATS`, list inset, row height |
| NVS keys | `shield_up`, `sort_mode`, `theme_idx`, `madctl`, `cal_ver`, `touch_rot`, `touch_flip`, `touch_cal` |

---

## Known limits

- Classic BR/EDR: stack comes up (`dual-mode controller up`) but no classic device
  has been captured yet, so finds are unverified.
- SPI bus sharing between display, SD card and touch is fragile — see the bus
  topology note and TODO.md.
- No persistence of seen devices across reboots; no log rotation.
- ESP32 supports BLE 4.2 only — no extended advertising, no Coded PHY.
- Randomised MACs rotate, so "same device" only holds within a session.
- A device that advertises no name and no manufacturer data can only be shown as a
  MAC tail.