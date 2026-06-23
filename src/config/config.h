#pragma once

// ── Hardware version ─────────────────────────────────────────────────────────
// CYD 2.8" boards have two hardware revisions:
//   1 = ESP32-32E (1-USB, original)               → standard landscape rotation 1
//   2 = 2-USB (newer, 2 USB ports)                 → landscape + mirror Y
// The 2-USB version has the LCD physically flipped compared to the 1-USB.
//
// Default is 1 (ESP32-32E).
// For 2-USB, override in platformio.ini build_flags with -DCYD_USB_VERSION=2
#ifndef CYD_USB_VERSION
#define CYD_USB_VERSION  1
#endif

// ── Display (ILI9341 on HSPI) ─────────────────────────────────────────────────
#define TFT_MOSI   13
#define TFT_MISO   12
#define TFT_SCLK   14
#define TFT_CS     15
#define TFT_DC      2
#define TFT_RST    -1
#define TFT_BL     21

// ── Touch (XPT2046 on VSPI — remapped) ───────────────────────────────────────
#define TOUCH_CS   33
#define TOUCH_IRQ  36
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_SCLK 25
// Calibration — adjust if touches feel offset
#define TOUCH_X_MIN  300
#define TOUCH_X_MAX 3900
#define TOUCH_Y_MIN  200
#define TOUCH_Y_MAX 3800

// ── RGB LED ──────────────────────────────────────────────────────────────────
#define LED_R 22
#define LED_G 16
#define LED_B 17

// ── Screen geometry ───────────────────────────────────────────────────────────
#define SCREEN_W   320
#define SCREEN_H   240
#define TOPBAR_H    22
#define BOTBAR_H    22
#define CONTENT_Y  (TOPBAR_H + 1)
#define CONTENT_H  (SCREEN_H - TOPBAR_H - BOTBAR_H - 2)

// ── Power button ──────────────────────────────────────────────────────────────
#define PWR_BTN_X   306
#define PWR_BTN_Y   10
#define PWR_BTN_R   7

// ── Bluetooth scan constants ──────────────────────────────────────────────────
#define BT_DEVICE_MAX      60
#define BT_NAME_LEN        32
#define BT_MAC_LEN         6
#define BT_MAC_STR_LEN     18      // "XX:XX:XX:XX:XX:XX\0"
#define BT_MFG_NAME_LEN    20

// Scan timing
#define BLE_SCAN_INTERVAL_MS   100    // BLE scan interval (advertising channel hop)
#define BLE_SCAN_WINDOW_MS      99    // BLE scan window (near-continuous)
#define CLASSIC_INQUIRY_SEC     10    // Classic BT inquiry duration
#define CLASSIC_INQUIRY_INTERVAL_MS  60000  // How often to run Classic inquiry
#define DEVICE_AGE_MAX          20    // Radar frames before expiry (~2s at 80ms/frame)

// Shield / Alert
#define SHIELD_SCAN_INTERVAL_MS  3000  // Faster scanning when shields up

// History graph
#define HISTORY_POINTS  15

// RSSI defaults
#define RSSI_FILTER_DEFAULT  -100   // Show all
#define RSSI_USABLE          -70    // "Strong" signal threshold

// ── Colors (RGB565) ───────────────────────────────────────────────────────────
#define COL_BG          0x0000
#define COL_WHITE       0xFFFF
#define COL_BLACK       0x0000
#define COL_RED         0xF800
#define COL_DARK_RED    0x9000
#define COL_GREEN       0x07E0
#define COL_DARK_GREEN  0x0300
#define COL_GOLD        0xFDA0
#define COL_PALE_YELLOW 0xFFEF
#define COL_LIGHT_GRAY  0xC618
#define COL_MID_GRAY    0x8410
#define COL_DIM_GRAY    0x4208
#define COL_AMBER       0xFD40
#define COL_BLUE        0x001F
#define COL_DARK_BLUE   0x0010
#define COL_CYAN        0x07FF
