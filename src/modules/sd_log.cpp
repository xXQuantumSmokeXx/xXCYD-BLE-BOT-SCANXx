#include "sd_log.h"
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "../config/config.h"

static bool     s_ready   = false;
static bool     s_enabled = true;
static char     s_buf[4096];
static size_t   s_len     = 0;
static uint32_t s_lastFlushMs = 0;
static uint32_t s_records = 0;
static const char *s_path = "/ble.jsonl";

// The SD card shares the display's SPI bus. The Arduino core's global SPI object is
// VSPI, which the touch controller also uses, so mounting the card moves bus pins;
// touchReinit() must be called afterwards (main.cpp does this).
#if SD_LOG_ENABLE
bool sdLogInit() {
    // Do NOT re-initialise the bus here: TFT_eSPI has already configured it, and
    // calling SPI.begin() again is what made the mount fail earlier.
    bool ok = SD.begin(SD_CS, SPI, 4000000);
    int  mode = ok ? 1 : 0;
    if (!ok) {
        SD.end();
        ok = SD.begin(SD_CS, SPI, 1000000);
        mode = ok ? 2 : 0;
    }

    s_ready = ok;
    if (!ok) {
        Serial.println("SD: mount failed (global SPI, 4 MHz then 1 MHz)");
        return false;
    }

    uint64_t used = 0;
    File f = SD.open(s_path, FILE_READ);
    if (f) { used = f.size(); f.close(); }
    Serial.printf("SD: mounted (cfg %d, type %d, %llu MB), log %s (%llu bytes)\n",
                  mode, (int)SD.cardType(), SD.cardSize() / (1024ULL * 1024ULL),
                  s_path, (unsigned long long)used);
    return true;
}
#else
bool sdLogInit() { s_ready = false; return false; }   // parked, see config.h
#endif

bool sdLogReady() { return s_ready; }
bool sdLogEnabled() { return s_enabled && s_ready; }
void sdLogSetEnabled(bool on) { s_enabled = on; }
uint32_t sdLogRecordCount() { return s_records; }

void sdLogAppend(const char *line) {
    if (!sdLogEnabled()) return;
    const size_t n = strlen(line);
    if (n == 0) return;
    // Drop the record rather than split it if it cannot fit the buffer.
    if (s_len + n + 1 > sizeof(s_buf)) sdLogFlush(true);
    if (s_len + n + 1 > sizeof(s_buf)) return;
    memcpy(s_buf + s_len, line, n);
    s_len += n;
    s_buf[s_len++] = '\n';
    s_records++;
    if (s_len > sizeof(s_buf) * 3 / 4) sdLogFlush(true);
}

bool sdLogFlush(bool force) {
    if (!sdLogEnabled() || s_len == 0) return false;
    const uint32_t now = millis();
    if (!force && now - s_lastFlushMs < 2000) return false;
    File f = SD.open(s_path, FILE_APPEND);
    if (f) {
        f.write((const uint8_t *)s_buf, s_len);
        f.close();
        s_len = 0;
        s_lastFlushMs = now;
        return true;
    } else {
        Serial.println("SD: append failed");
    }
    return false;
}