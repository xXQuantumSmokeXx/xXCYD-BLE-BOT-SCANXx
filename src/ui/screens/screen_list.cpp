#include "screen_list.h"
#include "../theme.h"
#include "../../config/config.h"
#include "../../modules/bt_scanner.h"
#include <cstdio>
#include <cstring>

// ── Layout ────────────────────────────────────────────────────────────────
static const int MY_BOTBAR_H = 28;  // thicker than config.h BOTBAR_H (22) — room for buttons
static const int LIST_Y    = TOPBAR_H + 1;
static const int ROW_H     = 12;

// Bottom bar: 3 buttons centered evenly
// BTN1_W + BTN2_W + BTN3_W = 70+74+70 = 214, screen=320, gap=(320-214)/4=26.5→26
static const int BTN_Y     = SCREEN_H - MY_BOTBAR_H + 3;
static const int BTN_H     = 22;
static const int BTN_R     = 5;
static const int BTN1_W    = 70;
static const int BTN2_W    = 74;
static const int BTN3_W    = 70;
static const int BTN_GAP   = 26;
static const int BTN1_X    = BTN_GAP;
static const int BTN2_X    = BTN1_X + BTN1_W + BTN_GAP;
static const int BTN3_X    = BTN2_X + BTN2_W + BTN_GAP;

// Power ring
static const int PWR_X     = SCREEN_W - 14;
static const int PWR_Y     = 11;
static const int PWR_R     = 8;

// Theme dot (lower-right corner)
static const int THEME_X   = SCREEN_W - 10;
static const int THEME_Y   = SCREEN_H - 13;
static const int THEME_R   = 6;

// ═══════════════════════════════════════════════════════════════════════════
//  Helpers
// ═══════════════════════════════════════════════════════════════════════════

static void drawBtn(TFT_eSPI &tft, int x, int y, int w, int h, int r,
                     const char *label, uint16_t color, bool filled) {
    if (filled) tft.fillRoundRect(x, y, w, h, r, color);
    else        tft.fillRoundRect(x, y, w, h, r, COL_BG);
    tft.drawRoundRect(x, y, w, h, r, color);
    if (!filled) {
        tft.drawRoundRect(x+1, y+1, w-2, h-2, r, color);
    }
    tft.setTextFont(1);
    tft.setTextColor(filled ? COL_BG : color, filled ? color : COL_BG);
    int tw = tft.textWidth(label);
    tft.setCursor(x + (w - tw) / 2, y + (h - 8) / 2 + 1);
    tft.print(label);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Draw
// ═══════════════════════════════════════════════════════════════════════════

void screenListDraw(TFT_eSPI &tft) {
    tft.fillScreen(COL_BG);

    int count = btScannerGetDeviceCount();
    const BTDeviceEntry *devs = btScannerGetDevices();
    bool shieldsUp = btScannerGetShieldUp();
    ScanState state = btScannerGetState();
    SortMode sortMode = btScannerGetSortMode();

    // ═══════════════════════════════════════════════════════════════════════
    //  TOP BAR
    // ═══════════════════════════════════════════════════════════════════════

    tft.fillRect(0, 0, SCREEN_W, TOPBAR_H, COL_BG);
    tft.drawFastHLine(0, TOPBAR_H - 1, SCREEN_W, g_themeColor);
    // Corner ticks
    tft.drawFastHLine(0, 0, 10, g_themeColor);
    tft.drawFastVLine(0, 0, 6, g_themeColor);
    tft.drawFastHLine(SCREEN_W - 10, 0, 10, g_themeColor);
    tft.drawFastVLine(SCREEN_W - 1, 0, 6, g_themeColor);

    // "X Devices online" — themed, not dim
    tft.setTextFont(1);
    tft.setTextColor(g_themeColor, COL_BG);
    tft.setCursor(14, 7);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d Device%s online", count, count == 1 ? "" : "s");
    tft.print(buf);

    // Classic BT indicator (if available)
    if (btScannerGetClassicEnabled()) {
        tft.setTextColor(COL_BLUE, COL_BG);
        tft.setCursor(14, 19);
        tft.print("+BT");
    }

    // Power ring — top right
    tft.drawCircle(PWR_X, PWR_Y, PWR_R, g_themeColor);
    tft.drawCircle(PWR_X, PWR_Y, PWR_R - 1, g_themeColor);
    tft.drawLine(PWR_X, PWR_Y - PWR_R + 4, PWR_X, PWR_Y, g_themeColor);
    tft.drawLine(PWR_X - 1, PWR_Y - PWR_R + 4, PWR_X + 1, PWR_Y - PWR_R + 4, g_themeColor);

    // Shield status — moved left so it doesn't crowd the power ring
    if (shieldsUp) {
        tft.setTextColor(COL_RED, COL_BG);
        tft.setCursor(PWR_X - 70, 7);
        tft.print("SHIELD UP");
    }

    // ═══════════════════════════════════════════════════════════════════════
    //  DEVICE ROWS
    // ═══════════════════════════════════════════════════════════════════════

    int maxRows = (SCREEN_H - MY_BOTBAR_H - LIST_Y) / ROW_H;
    if (maxRows < 0) maxRows = 0;
    int visible = (maxRows < count) ? maxRows : count;

    for (int row = 0; row < visible; row++) {
        const BTDeviceEntry &dev = devs[row];
        int y = LIST_Y + row * ROW_H;

        uint16_t rowBg = COL_BG;

        // ── RSSI bar ──
        int barW = map(dev.rssi, -100, -30, 1, 22);
        if (barW < 1) barW = 1;
        uint16_t barCol = g_themeColor;
        if (dev.rssi >= RSSI_USABLE) barCol = COL_GREEN;
        else if (dev.rssi < -90) barCol = COL_RED;
        if (dev.isAlert) barCol = COL_RED;
        tft.fillRect(2, y + 3, barW, ROW_H - 7, barCol);

        // ── RSSI number ──
        tft.setTextFont(1);
        tft.setTextColor(COL_WHITE, rowBg);
        tft.setCursor(26, y + 2);
        char rbuf[5]; snprintf(rbuf, sizeof(rbuf), "%d", dev.rssi);
        tft.print(rbuf);

        // ── Name or MAC ──
        const char *label = dev.name[0] ? dev.name : dev.macStr;
        char nameBuf[22];
        int len = strlen(label);
        if (len > 20) { memcpy(nameBuf, label, 19); nameBuf[19]='.'; nameBuf[20]='\0'; }
        else { strcpy(nameBuf, label); }

        uint16_t nameCol = dev.isAlert ? COL_RED :
            (dev.source == BTSource::Classic ? COL_BLUE : g_themeColor);
        tft.setTextColor(nameCol, rowBg);
        tft.setCursor(56, y + 2);
        tft.print(nameBuf);

        // ── Type / manufacturer (right-aligned) ──
        const char *extra = "";
        if (dev.deviceType != BTDeviceType::Unknown)
            extra = btDeviceTypeName(dev.deviceType);
        else if (strcmp(dev.manufacturer, "Unknown") != 0)
            extra = dev.manufacturer;
        tft.setTextColor(COL_WHITE, rowBg);
        int ew = tft.textWidth(extra);
        tft.setCursor(SCREEN_W - ew - 3, y + 2);
        tft.print(extra);

        // Row separator
        if (row < visible - 1)
            tft.drawFastHLine(0, y + ROW_H - 1, SCREEN_W, 0x1082);
    }

    // ── Overflow ──
    if (count > visible) {
        tft.setTextFont(1);
        tft.setTextColor(g_themeColor, COL_BG);
        snprintf(buf, sizeof(buf), "+%d more", count - visible);
        int ow = tft.textWidth(buf);
        tft.setCursor(SCREEN_W - ow - 4, LIST_Y + visible * ROW_H);
        tft.print(buf);
    }

    // ── Empty state ──
    if (count == 0) {
        tft.setTextFont(2);
        tft.setTextColor(g_themeColor, COL_BG);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("No Devices", SCREEN_W/2, SCREEN_H/2 - 12);
        tft.setTextFont(1);
        tft.drawString("Searching for Bluetooth signals...", SCREEN_W/2, SCREEN_H/2 + 8);
    }

    // ═══════════════════════════════════════════════════════════════════════
    //  BOTTOM BAR — actual buttons
    // ═══════════════════════════════════════════════════════════════════════

    int botTop = SCREEN_H - MY_BOTBAR_H;
    tft.fillRect(0, botTop, SCREEN_W, MY_BOTBAR_H, COL_BG);
    tft.drawFastHLine(0, botTop, SCREEN_W, g_themeColor);
    // Bottom corner ticks
    tft.drawFastHLine(0, SCREEN_H - 1, 10, g_themeColor);
    tft.drawFastVLine(0, SCREEN_H - 6, 6, g_themeColor);
    tft.drawFastHLine(SCREEN_W - 10, SCREEN_H - 1, 10, g_themeColor);
    tft.drawFastVLine(SCREEN_W - 1, SCREEN_H - 6, 6, g_themeColor);

    // ── [SHIELD] button ──
    const char *shLabel = shieldsUp ? "ALERT ON" : "ALERT OFF";
    uint16_t shCol = shieldsUp ? COL_RED : g_themeColor;
    drawBtn(tft, BTN1_X, BTN_Y, BTN1_W, BTN_H, BTN_R, shLabel, shCol, false);  // never filled, just text color change

    // ── [SORT] button ──
    const char *sortLabel;
    switch (sortMode) {
        case SortMode::RSSI:  sortLabel = "RSSI"; break;
        case SortMode::Name:  sortLabel = "NAME"; break;
        case SortMode::DeviceType: sortLabel = "TYPE"; break;
        case SortMode::FirstSeen:  sortLabel = "NEW"; break;
    }
    drawBtn(tft, BTN2_X, BTN_Y, BTN2_W, BTN_H, BTN_R, sortLabel, g_themeColor, false);

    // ── [SCAN] button — tap to stop/start, red text only, never filled ──
    bool scanning = (state == ScanState::BLEScanning || state == ScanState::ClassicInquiry);
    drawBtn(tft, BTN3_X, BTN_Y, BTN3_W, BTN_H, BTN_R,
            scanning ? "STOP" : "SCAN",
            scanning ? COL_RED : g_themeColor,
            false);  // never filled

    // ── Theme color dot (lower-right corner) ──
    tft.fillCircle(THEME_X, THEME_Y, THEME_R, g_themeColor);
    tft.drawCircle(THEME_X, THEME_Y, THEME_R, COL_WHITE);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Touch hit testing
// ═══════════════════════════════════════════════════════════════════════════

void screenListTap(int tx, int ty) {
    (void)tx; (void)ty;
}

bool screenListHitShield(int tx, int ty) {
    return (tx >= BTN1_X && tx <= BTN1_X + BTN1_W &&
            ty >= BTN_Y && ty <= BTN_Y + BTN_H);
}

bool screenListHitSort(int tx, int ty) {
    return (tx >= BTN2_X && tx <= BTN2_X + BTN2_W &&
            ty >= BTN_Y && ty <= BTN_Y + BTN_H);
}

bool screenListHitScan(int tx, int ty) {
    return (tx >= BTN3_X && tx <= BTN3_X + BTN3_W &&
            ty >= BTN_Y && ty <= BTN_Y + BTN_H);
}

bool screenListHitPower(int tx, int ty) {
    int dx = tx - PWR_X;
    int dy = ty - PWR_Y;
    return (dx*dx + dy*dy <= (PWR_R + 4)*(PWR_R + 4));
}

bool screenListHitTheme(int tx, int ty) {
    int dx = tx - THEME_X;
    int dy = ty - THEME_Y;
    return (dx*dx + dy*dy <= (THEME_R + 4)*(THEME_R + 4));
}
