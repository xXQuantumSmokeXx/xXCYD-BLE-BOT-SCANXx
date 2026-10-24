#include "screen_list.h"
#include "../theme.h"
#include "../../config/config.h"
#include "../../modules/bt_scanner.h"
#include <cstdio>
#include <cstring>

// ── Layout ────────────────────────────────────────────────────────────────
#define SHOW_ADV_STATS 0   // top-bar diagnostic; 1 re-enables rep/nm counters

static const int MY_BOTBAR_H = 28;  // thicker than config.h BOTBAR_H (22) — room for buttons
static const int LIST_Y    = TOPBAR_H + 1;
static const int ROW_H     = 12;

// Bottom bar: ALERT pinned to the left corner, STOP to the right corner, SORT centred
static const int BTN_Y      = SCREEN_H - MY_BOTBAR_H + 3;
static const int BTN_H      = 22;
static const int BTN_R      = 5;
static const int BTN1_W     = 70;
static const int BTN2_W     = 74;
static const int BTN3_W     = 70;
static const int BTN_MARGIN = 4;
static const int BTN1_X     = BTN_MARGIN;
static const int BTN3_X     = SCREEN_W - BTN_MARGIN - BTN3_W;
static const int BTN2_X     = (SCREEN_W - BTN2_W) / 2;

// Power ring
static const int PWR_X     = SCREEN_W - 14;
static const int PWR_Y     = 11;
static const int PWR_R     = 8;

// Theme dot: top bar, immediately left of the power ring
static const int THEME_R   = 5;
static const int THEME_X   = PWR_X - 26;
static const int THEME_Y   = PWR_Y;

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

// Builds the left-column label: advertised name (or MAC tail) plus vendor, truncated
// to fit. Shared by the measuring pass and the drawing pass.
static void buildRowLabel(const BTDeviceEntry &dev, char *out, size_t outLen) {
    char buf[52];
    const char *base = dev.name[0] ? dev.name : (dev.macStr + 9);
    const char *mark = (!dev.name[0] && dev.hasAdvertisedName) ? "*" : "";
    if (dev.manufacturer[0])
        snprintf(buf, sizeof(buf), "%s%s %s", base, mark, dev.manufacturer);
    else
        snprintf(buf, sizeof(buf), "%s%s", base, mark);

    const int maxChars = 24;
    const int len = (int)strlen(buf);
    if (len > maxChars) {
        memcpy(out, buf, maxChars - 1);
        out[maxChars - 1] = '.';
        out[maxChars] = '\0';
    } else {
        snprintf(out, outLen, "%s", buf);
    }
}
// Right-column text: address-type letter plus device class when known.
static void buildRowType(const BTDeviceEntry &dev, char *out, size_t outLen) {
    const char addr = (dev.addrType == 0) ? 'P' : 'R';
    if (dev.deviceType != BTDeviceType::Unknown)
        snprintf(out, outLen, "%c %s", addr, btDeviceTypeName(dev.deviceType));
    else
        snprintf(out, outLen, "%c", addr);
}

// Row geometry from the last draw, so a tap can be mapped back to a device.
static int s_scroll = 0;   // index of the first visible device
static int s_rowY0 = 0;
static int s_rowCount = 0;
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



    // Power ring — top right
    tft.drawCircle(PWR_X, PWR_Y, PWR_R, g_themeColor);
    tft.drawCircle(PWR_X, PWR_Y, PWR_R - 1, g_themeColor);
    tft.drawLine(PWR_X, PWR_Y - PWR_R + 4, PWR_X, PWR_Y, g_themeColor);
    tft.drawLine(PWR_X - 1, PWR_Y - PWR_R + 4, PWR_X + 1, PWR_Y - PWR_R + 4, g_themeColor);

    // Shield status — moved left so it doesn't crowd the power ring
#if SHOW_ADV_STATS
    {
        uint32_t rep = 0, nm = 0;
        btScannerGetAdvStats(&rep, &nm);
        tft.setTextColor(COL_DIM_GRAY, COL_BG);
        tft.setCursor(150, 7);
        tft.printf("rep %lu nm %lu", (unsigned long)rep, (unsigned long)nm);
    }
#endif

    if (shieldsUp) {
        tft.setTextColor(COL_RED, COL_BG);
        const int sw = tft.textWidth("SHIELD UP");
        tft.setCursor(THEME_X - THEME_R - sw - 8, 7);
        tft.print("SHIELD UP");
    }

    // ═══════════════════════════════════════════════════════════════════════
    //  DEVICE ROWS
    // ═══════════════════════════════════════════════════════════════════════

    const int listAreaH = SCREEN_H - MY_BOTBAR_H - LIST_Y;
    int maxRows = listAreaH / ROW_H;
    if (maxRows < 0) maxRows = 0;
    int visible = (maxRows < count) ? maxRows : count;

    // Clamp the scroll offset: devices come and go, so the list can shrink under it.
    int maxScroll = count - visible;
    if (maxScroll < 0) maxScroll = 0;
    if (s_scroll > maxScroll) s_scroll = maxScroll;
    if (s_scroll < 0) s_scroll = 0;

    // Clear the whole list area: rows move when the block is centred, and the old
    // row separators would otherwise stay painted on the panel.
    tft.fillRect(0, LIST_Y, SCREEN_W, listAreaH, COL_BG);

    // Centre the block vertically, so a short list sits mid-screen rather than
    // stacking against the top bar.
    int listStartY = LIST_Y + (listAreaH - visible * ROW_H) / 2;
    if (listStartY < LIST_Y) listStartY = LIST_Y;
    s_rowY0 = listStartY;
    s_rowCount = visible;

    // Equal side margins so the list reads as a centred column.
    const int listInset = 24;

    // Measure the widest row first so every row shares one left edge: the block is
    // centred as a whole, and each bar sits right beside its own number.
    int maxCluster = 0;
    for (int row = 0; row < visible; row++) {
        char rb[6];
        snprintf(rb, sizeof(rb), "%d", devs[s_scroll + row].rssi);
        char nm[32];
        buildRowLabel(devs[s_scroll + row], nm, sizeof(nm));
        char ty[24];
        buildRowType(devs[s_scroll + row], ty, sizeof(ty));
        int cw = 22 + 6 + tft.textWidth(rb) + 8 + tft.textWidth(nm) + 8 + tft.textWidth(ty);
        if (cw > maxCluster) maxCluster = cw;
    }
    int blockX = (SCREEN_W - maxCluster) / 2;
    if (blockX < 2) blockX = 2;

    for (int row = 0; row < visible; row++) {
        const BTDeviceEntry &dev = devs[s_scroll + row];
        int y = listStartY + row * ROW_H;
        uint16_t rowBg = COL_BG;

        int barW = map(dev.rssi, -100, -30, 1, 22);
        if (barW < 1) barW = 1;
        uint16_t barCol = g_themeColor;
        if (dev.rssi >= RSSI_USABLE) barCol = COL_GREEN;
        else if (dev.rssi < -90) barCol = COL_RED;
        if (dev.isAlert) barCol = COL_RED;

        tft.setTextFont(1);
        char rbuf[6];
        snprintf(rbuf, sizeof(rbuf), "%d", dev.rssi);
        const int rw = tft.textWidth(rbuf);

        char nameBuf[32];
        buildRowLabel(dev, nameBuf, sizeof(nameBuf));
        const int lw = tft.textWidth(nameBuf);

        char typeBuf[24];
        buildRowType(dev, typeBuf, sizeof(typeBuf));
        const char *extra = typeBuf;
        const int ew = tft.textWidth(extra);

        const int barSlot = 22, gap1 = 6, gap2 = 8, gap3 = 8;

        // Bar right-aligned inside its slot so the gauge end meets the number.
        tft.fillRect(blockX + barSlot - barW, y + 3, barW, ROW_H - 7, barCol);

        tft.setTextColor(COL_WHITE, rowBg);
        tft.setCursor(blockX + barSlot + gap1, y + 2);
        tft.print(rbuf);

        uint16_t nameCol = dev.isAlert ? COL_RED :
            (dev.source == BTSource::Classic ? COL_BLUE : g_themeColor);
        tft.setTextColor(nameCol, rowBg);
        tft.setCursor(blockX + barSlot + gap1 + rw + gap2, y + 2);
        tft.print(nameBuf);

        if (ew > 0) {
            tft.setTextColor(COL_WHITE, rowBg);
            tft.setCursor(blockX + barSlot + gap1 + rw + gap2 + lw + gap3, y + 2);
            tft.print(extra);
        }


    }

    // ── Scroll position and scrollbar ──
    if (maxScroll > 0) {
        tft.setTextFont(1);
        tft.setTextColor(COL_DIM_GRAY, COL_BG);
        // Position readout lives in the top bar; under the list it collided with the
        // bottom bar once the rows filled up.
        snprintf(buf, sizeof(buf), "%d-%d / %d", s_scroll + 1, s_scroll + visible, count);
        tft.setCursor(136, 7);
        tft.print(buf);

        const int barX = SCREEN_W - 5;
        const int barY = listStartY;
        const int barH = visible * ROW_H - 2;
        tft.drawFastVLine(barX, barY, barH, COL_DIM_GRAY);
        int thumbH = (barH * visible) / (count > 0 ? count : 1);
        if (thumbH < 8) thumbH = 8;
        const int thumbY = barY + ((barH - thumbH) * s_scroll) / maxScroll;
        tft.fillRect(barX - 1, thumbY, 3, thumbH, g_themeColor);
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

// Maps a tap to a visible row, or -1.
int screenListRowAt(int tx, int ty) {
    (void)tx;
    if (s_rowCount <= 0) return -1;
    if (ty < s_rowY0 || ty >= s_rowY0 + s_rowCount * ROW_H) return -1;
    const int idx = (ty - s_rowY0) / ROW_H;
    if (idx < 0 || idx >= s_rowCount) return -1;
    return s_scroll + idx;
}

// Full-screen detail for one device. Tapping anywhere closes it.
void screenDetailDraw(TFT_eSPI &tft, int idx) {
    const BTDeviceEntry *dev = btScannerGetDevice(idx);
    tft.fillScreen(COL_BG);

    if (!dev) {
        tft.setTextDatum(MC_DATUM);
        tft.setTextFont(2);
        tft.setTextColor(g_themeColor, COL_BG);
        tft.drawString("Device gone", SCREEN_W / 2, SCREEN_H / 2);
        tft.setTextDatum(TL_DATUM);
        return;
    }

    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(g_themeColor, COL_BG);
    tft.setCursor(6, 4);
    tft.print(dev->name[0] ? dev->name : (dev->manufacturer[0] ? dev->manufacturer : "unknown device"));
    tft.drawFastHLine(0, 22, SCREEN_W, g_themeColor);

    tft.setTextFont(1);
    int y = 28;
    char buf[52];
    auto field = [&](const char *k, const char *v) {
        tft.setTextColor(COL_DIM_GRAY, COL_BG);
        tft.setCursor(6, y);
        tft.print(k);
        tft.setTextColor(COL_WHITE, COL_BG);
        tft.setCursor(80, y);
        tft.print(v);
        y += 13;
    };

    field("MAC", dev->macStr);
    field("Vendor", dev->manufacturer[0] ? dev->manufacturer : "-");
    field("Address", dev->addrType == 0 ? "public / stable" : "random / rotating");

    snprintf(buf, sizeof(buf), "%s", btDeviceTypeName(dev->deviceType));
    field("Type", buf);
    snprintf(buf, sizeof(buf), "%d dBm", (int)dev->rssi);
    field("RSSI", buf);
    if (dev->hasTxPower) { snprintf(buf, sizeof(buf), "%d dBm", (int)dev->txPower); }
    else { snprintf(buf, sizeof(buf), "-"); }
    field("TX", buf);
    if (dev->hasAppearance) { snprintf(buf, sizeof(buf), "0x%04X", dev->appearance); }
    else { snprintf(buf, sizeof(buf), "-"); }
    field("Appear", buf);
    if (dev->hasMfgData) { snprintf(buf, sizeof(buf), "0x%04X", dev->manufacturerId); }
    else { snprintf(buf, sizeof(buf), "-"); }
    field("Co.ID", buf);
    field("Source", btSourceLabel(dev->source));
    snprintf(buf, sizeof(buf), "%u", (unsigned)dev->scanCount);
    field("Reports", buf);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(g_themeColor, COL_BG);
    tft.drawString("TAP TO CLOSE", SCREEN_W / 2, SCREEN_H - 12);
    tft.setTextDatum(TL_DATUM);
}
// Scroll by whole pages; used by swipe gestures.
void screenListScrollBy(int rows) {
    s_scroll += rows;
    if (s_scroll < 0) s_scroll = 0;
    const int count = btScannerGetDeviceCount();
    int maxScroll = count - s_rowCount;
    if (maxScroll < 0) maxScroll = 0;
    if (s_scroll > maxScroll) s_scroll = maxScroll;
}

void screenListScrollPage(int dir) {
    const int page = (s_rowCount > 1) ? s_rowCount - 1 : 1;
    screenListScrollBy(dir * page);
}

void screenListScrollTop() { s_scroll = 0; }