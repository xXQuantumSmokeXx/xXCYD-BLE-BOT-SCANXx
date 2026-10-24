#pragma once
#include <cstddef>
#include <stdint.h>

// SD-card JSONL logging. The card shares the HSPI bus with the display, so writes
// are buffered and flushed in one burst; the firmware is single-threaded, so a redraw
// can never interleave with a write.
bool   sdLogInit();
bool   sdLogReady();
void   sdLogAppend(const char *line);
bool   sdLogFlush(bool force = false);
void   sdLogSetEnabled(bool on);
bool   sdLogEnabled();
uint32_t sdLogRecordCount();