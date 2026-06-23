#include "screenshot.h"

// Stub — full implementation mirrors CYD-Weather's screenshot module.
// For now, serial capture ('S') is handled inline in main.cpp loop().

static TFT_eSPI *s_tft = nullptr;
static void (*s_redrawFn)(TFT_eSPI &) = nullptr;

void screenshotInit(TFT_eSPI &tft, void (*redrawFn)(TFT_eSPI &)) {
    s_tft = &tft;
    s_redrawFn = redrawFn;
}

void screenshotLoop() {
    // Stub — no-op until Phase 8 integration
}

bool screenshotNeedsRedraw() {
    return false;
}
