#pragma once
#include <TFT_eSPI.h>

// Initialize screenshot capture — registers redraw callback with CYD-ScreenCapture protocol
void screenshotInit(TFT_eSPI &tft, void (*redrawFn)(TFT_eSPI &));

// Call each loop iteration — checks serial for capture commands
void screenshotLoop();

// Returns true if screenshot subsystem requires a redraw
bool screenshotNeedsRedraw();
