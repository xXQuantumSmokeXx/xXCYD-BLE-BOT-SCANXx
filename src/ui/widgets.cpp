#include "widgets.h"
#include "theme.h"
#include "../config/config.h"
#include <cstring>
#include <cstdio>

// Arm lengths for corner bracket ticks
#define TK_H  10   // horizontal arm
#define TK_V   6   // vertical arm

// ── Battery reading ──────────────────────────────────────────────────────────

int batteryPct() {
#if CYD_USB_VERSION == 2
    // 2USB: battery voltage divider on GPIO34
    int raw = analogRead(34);
    float v = (raw / 4095.0f) * 3.3f * 2.0f;  // voltage divider 1:1
    int pct = (int)((v - 3.3f) / (4.2f - 3.3f) * 100.0f);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
#else
    // 1USB: GPIO34 may also have battery divider
    int raw = analogRead(34);
    if (raw < 100) return -1;  // no battery connected
    float v = (raw / 4095.0f) * 3.3f * 2.0f;
    int pct = (int)((v - 3.3f) / (4.2f - 3.3f) * 100.0f);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
#endif
}

// ── Topbar ───────────────────────────────────────────────────────────────────

void drawTopbar(TFT_eSPI &tft, const char *deviceCount, const char *screenLabel, const char *timeStr) {
    tft.fillRect(0, 0, SCREEN_W, TOPBAR_H, COL_BG);

    // ── Device count — left ──
    if (deviceCount && deviceCount[0]) {
        tft.setTextFont(FONT_MD);
        tft.setTextColor(g_themeColor, COL_BG);
        tft.setCursor(TK_H + 3, 3);
        tft.print(deviceCount);
    }

    // ── Screen label — centered, white ──
    if (screenLabel && screenLabel[0]) {
        tft.setTextFont(FONT_MD);
        tft.setTextColor(COL_WHITE, COL_BG);
        int lw = tft.textWidth(screenLabel);
        int lx = (SCREEN_W - lw) / 2;
        tft.setCursor(lx, 3);
        tft.print(screenLabel);
    }

    // ── Time — right ──
    if (timeStr && timeStr[0]) {
        tft.setTextFont(FONT_MD);
        int tw = tft.textWidth(timeStr);
        tft.setTextColor(COL_WHITE, COL_BG);
        int timeX = SCREEN_W - tw - TK_H - 3;
        if (timeX < 160) timeX = 160;
        tft.setCursor(timeX, 3);
        tft.print(timeStr);
    }

    // ── Solid border at bottom of topbar ──
    tft.drawFastHLine(0, TOPBAR_H - 1, SCREEN_W, g_themeColor);

    // ── Corner bracket ticks ──
    tft.drawFastHLine(0,              0, TK_H, g_themeColor);
    tft.drawFastVLine(0,              0, TK_V, g_themeColor);
    tft.drawFastHLine(SCREEN_W - TK_H, 0, TK_H, g_themeColor);
    tft.drawFastVLine(SCREEN_W - 1,    0, TK_V, g_themeColor);

    // ── Small filled squares flanking the screen label ──
    if (screenLabel && screenLabel[0]) {
        int lw = tft.textWidth(screenLabel);
        int lx = (SCREEN_W - lw) / 2;
        tft.fillRect(lx - 6,      TOPBAR_H - 5, 3, 4, g_themeColor);
        tft.fillRect(lx + lw + 3, TOPBAR_H - 5, 3, 4, g_themeColor);
    }
}

void drawTopbarTime(TFT_eSPI &tft, const char *timeStr, const char *screenLabel) {
    // Clear right portion of topbar
    tft.fillRect(155, 0, SCREEN_W - 155, TOPBAR_H - 1, COL_BG);

    // Redraw centered screen label
    if (screenLabel && screenLabel[0]) {
        tft.setTextFont(FONT_MD);
        tft.setTextColor(COL_WHITE, COL_BG);
        int lw = tft.textWidth(screenLabel);
        tft.setCursor((SCREEN_W - lw) / 2, 3);
        tft.print(screenLabel);
    }

    // Draw time — right side
    if (timeStr && timeStr[0]) {
        tft.setTextFont(FONT_MD);
        int tw = tft.textWidth(timeStr);
        tft.setTextColor(COL_WHITE, COL_BG);
        int timeX = SCREEN_W - tw - TK_H - 3;
        if (timeX < 160) timeX = 160;
        tft.setCursor(timeX, 3);
        tft.print(timeStr);
    }

    // Redraw right-side corner ticks
    tft.drawFastHLine(SCREEN_W - TK_H, 0, TK_H, g_themeColor);
    tft.drawFastVLine(SCREEN_W - 1,    0, TK_V, g_themeColor);
}

// ── Bottombar ────────────────────────────────────────────────────────────────

void drawBottombar(TFT_eSPI &tft, const char *statusLabel, int activeScreen, int totalScreens) {
    int y0 = SCREEN_H - BOTBAR_H;
    tft.fillRect(0, y0, SCREEN_W, BOTBAR_H, COL_BG);

    int my = y0 + BOTBAR_H / 2;

    // ── Navigation arrows ──
    tft.setTextFont(FONT_MD);
    int arrowW = tft.textWidth(">");

    uint16_t lCol = (activeScreen > 0) ? g_themeColor : COL_DIM;
    tft.setTextColor(lCol, COL_BG);
    tft.setCursor(TK_H + 2, my - 8);
    tft.print("<");

    int rarrowX = SCREEN_W - TK_H - 2 - arrowW;
    uint16_t rCol = (activeScreen < totalScreens - 1) ? g_themeColor : COL_DIM;
    tft.setTextColor(rCol, COL_BG);
    tft.setCursor(rarrowX, my - 8);
    tft.print(">");

    // ── Battery % — just left of right arrow ──
    int batt = batteryPct();
    if (batt >= 0) {
        char bbuf[8];
        snprintf(bbuf, sizeof(bbuf), "%d%%", batt);
        tft.setTextFont(FONT_MD);
        int bw = tft.textWidth(bbuf);
        tft.setTextColor(COL_WHITE, COL_BG);
        tft.setCursor(rarrowX - bw - 4, my - 8);
        tft.print(bbuf);
    }

    // ── Page indicator dots or status label ──
    if (!statusLabel || !statusLabel[0]) {
        const int DS = 4, DG = 6;
        int total = totalScreens * DS + (totalScreens - 1) * DG;
        int dx = (SCREEN_W - total) / 2;
        int dy = my - DS / 2;
        for (int i = 0; i < totalScreens; i++) {
            if (i == activeScreen)
                tft.fillRect(dx, dy, DS, DS, g_themeColor);
            else
                tft.drawRect(dx, dy, DS, DS, COL_DIM);
            dx += DS + DG;
        }
    } else {
        // Centered status text
        tft.setTextFont(FONT_SM);
        tft.setTextColor(g_themeColor, COL_BG);
        int lw = tft.textWidth(statusLabel);
        tft.setCursor((SCREEN_W - lw) / 2, my - 4);
        tft.print(statusLabel);
    }

    // ── Solid border at top of bottombar ──
    tft.drawFastHLine(0, y0, SCREEN_W, g_themeColor);

    // ── Corner bracket ticks ──
    tft.drawFastHLine(0,              SCREEN_H - 1, TK_H, g_themeColor);
    tft.drawFastVLine(0,              SCREEN_H - TK_V, TK_V, g_themeColor);
    tft.drawFastHLine(SCREEN_W - TK_H, SCREEN_H - 1, TK_H, g_themeColor);
    tft.drawFastVLine(SCREEN_W - 1,    SCREEN_H - TK_V, TK_V, g_themeColor);
}

// ── Utilities ────────────────────────────────────────────────────────────────

void drawDivider(TFT_eSPI &tft, int y, uint16_t col) {
    if (col == 0) col = COL_DIM;
    tft.drawFastHLine(0, y, SCREEN_W, col);
}

void drawStat(TFT_eSPI &tft, int x, int y, const char *label, const char *value) {
    tft.setTextFont(FONT_SM);
    tft.setTextColor(COL_DIM, COL_BG);
    tft.setCursor(x, y);
    tft.print(label);
    tft.print(": ");
    tft.setTextColor(COL_WHITE, COL_BG);
    tft.print(value);
}
