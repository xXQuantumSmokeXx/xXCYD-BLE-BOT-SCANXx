#pragma once
#include <TFT_eSPI.h>

void screenListDraw(TFT_eSPI &tft);
bool screenListHitShield(int tx, int ty);
bool screenListHitSort(int tx, int ty);
bool screenListHitScan(int tx, int ty);
bool screenListHitPower(int tx, int ty);
bool screenListHitTheme(int tx, int ty);
