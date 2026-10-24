#pragma once
#include <TFT_eSPI.h>

void screenListDraw(TFT_eSPI &tft);
bool screenListHitShield(int tx, int ty);
bool screenListHitSort(int tx, int ty);
bool screenListHitScan(int tx, int ty);
bool screenListHitPower(int tx, int ty);
bool screenListHitTheme(int tx, int ty);

// Row under a tap, or -1. Opens the detail view for that device.
int  screenListRowAt(int tx, int ty);
void screenDetailDraw(TFT_eSPI &tft, int idx);

// Paging: swipe up/down scrolls a page at a time.
void screenListScrollBy(int rows);
void screenListScrollPage(int dir);
void screenListScrollTop();
