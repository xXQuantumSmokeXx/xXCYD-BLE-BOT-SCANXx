#pragma once
#include <TFT_eSPI.h>
#include <cstdint>

// ── Battery ──────────────────────────────────────────────────────────────────
// Returns battery percentage (0-100) or -1 if not measurable
int batteryPct();

// ── Topbar ───────────────────────────────────────────────────────────────────
// deviceCount: left-side label (e.g. "42 devs")
// screenLabel: centered screen name
// timeStr:     right-side time
void drawTopbar(TFT_eSPI &tft, const char *deviceCount, const char *screenLabel, const char *timeStr);
void drawTopbarTime(TFT_eSPI &tft, const char *timeStr, const char *screenLabel);

// ── Bottombar ────────────────────────────────────────────────────────────────
// statusLabel: optional centered text (e.g. "scanning..."), may be nullptr
// activeScreen: 0-based index
// totalScreens: total screen count
void drawBottombar(TFT_eSPI &tft, const char *statusLabel, int activeScreen, int totalScreens);

// ── Utility ──────────────────────────────────────────────────────────────────
void drawDivider(TFT_eSPI &tft, int y, uint16_t col = 0);
void drawStat(TFT_eSPI &tft, int x, int y, const char *label, const char *value);
