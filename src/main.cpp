/**
 * xXCYD-BLE-BOT-SCANXx — Dual-mode Bluetooth Scanner for CYD
 * Simplified single-screen list-only version.
 * No radar, no sleep timer — always-on passive scanner.
 */

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include "config/config.h"
#include "config/nvs_config.h"
#include "ui/theme.h"
#include "ui/theme_color.h"
#include "ui/widgets.h"
#include "touch/touch.h"
#include "modules/bt_scanner.h"
#include "ui/screens/screen_list.h"

static TFT_eSPI  tft;
static TFT_eSPI *disp = &tft;
static SPIClass  touchSPI(VSPI);

static bool        s_needsRedraw = true;
static bool        s_backlightOff = false;
static int         s_lastDeviceCount = -1;
static int         s_lastAlertCount = -1;

// ═══════════════════════════════════════════════════════════════════════════════
//  Boot Logo
// ═══════════════════════════════════════════════════════════════════════════════

static void drawBootLogo() {
    const int cx = SCREEN_W / 2, cy = 50, maxR = 36;
    for (int r = 1; r <= 3; r++) {
        int radius = maxR * r / 3;
        disp->drawCircle(cx, cy, radius, (r <= 2) ? g_themeColor : COL_DIM_GRAY);
    }
    disp->drawFastHLine(cx - maxR, cy, maxR * 2, COL_DIM_GRAY);
    disp->drawFastVLine(cx, cy - maxR, maxR * 2, COL_DIM_GRAY);
    float sw = 45.0f * PI / 180.0f;
    int sx = cx + (int)(maxR * cosf(sw));
    int sy = cy - (int)(maxR * sinf(sw));
    disp->drawLine(cx, cy, sx, sy, g_themeColor);
    float a1 = 40.0f * PI / 180.0f;
    int x1 = cx + (int)(maxR * cosf(a1));
    int y1 = cy - (int)(maxR * sinf(a1));
    disp->fillTriangle(cx, cy, sx, sy, x1, y1, g_themeColor);
    struct { float deg; int rFrac; bool bright; } blips[] = {
        {130,3,1},{200,2,0},{270,2,1},{310,3,0},{0,1,1},{90,2,1}};
    for (auto &b : blips) {
        float a = b.deg * PI / 180.0f;
        int r = maxR * b.rFrac / 3;
        disp->fillCircle(cx+(int)(r*cosf(a)), cy-(int)(r*sinf(a)),
                         b.bright?2:1, b.bright?COL_WHITE:COL_DIM_GRAY);
    }
    disp->fillCircle(cx, cy, 3, g_themeColor);
    disp->fillCircle(cx, cy, 1, COL_WHITE);
}

static void showSplash(const char *msg = nullptr) {
    disp->fillScreen(COL_BG);
    drawBootLogo();
    int tw;
    disp->setTextFont(4);
    disp->setTextColor(g_themeColor, COL_BG);
    tw = disp->textWidth("xXMayDayXx");
    disp->setCursor((SCREEN_W - tw) / 2, 104); disp->print("xXMayDayXx");
    disp->setTextFont(2);
    disp->setTextColor(COL_WHITE, COL_BG);
    tw = disp->textWidth("xXCYD-BLE-BOT-SCANXx");
    disp->setCursor((SCREEN_W - tw) / 2, 140); disp->print("xXCYD-BLE-BOT-SCANXx");
    disp->setTextColor(g_themeColor, COL_BG);
    tw = disp->textWidth("xXQuantum-SmokeXx");
    disp->setCursor((SCREEN_W - tw) / 2, 162); disp->print("xXQuantum-SmokeXx");
    disp->setTextFont(1);
    disp->setTextColor(COL_DIM_GRAY, COL_BG);
    const char *s = msg ? msg : "Loading...";
    tw = disp->textWidth(s);
    disp->setCursor((SCREEN_W - tw) / 2, 190); disp->print(s);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Power button
// ═══════════════════════════════════════════════════════════════════════════════

static void goToSleep() {
    disp->fillScreen(COL_BG);
    disp->setTextFont(2);
    disp->setTextColor(g_themeColor, COL_BG);
    disp->setTextDatum(MC_DATUM);
    disp->drawString("SLEEP", SCREEN_W/2, SCREEN_H/2);
    delay(500);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_36, 0);
    esp_deep_sleep_start();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  2USB calibration
// ═══════════════════════════════════════════════════════════════════════════════

#define CURRENT_CAL_VER  1
static uint8_t s_madctl = 0x80;

static void applyRotation() {
#if CYD_USB_VERSION == 2
    tft.setRotation(1);
    tft.writecommand(TFT_MADCTL);
    tft.writedata(s_madctl);
#else
    tft.setRotation(1);
#endif
}

static uint8_t madctlForCombo(int idx) {
    switch (idx & 3) {
        case 0:  return TFT_MAD_MV | TFT_MAD_BGR;
        case 1:  return TFT_MAD_MV | TFT_MAD_MY | TFT_MAD_BGR;
        case 2:  return 0x00;
        default: return TFT_MAD_MY;
    }
}

static void displayCalibrate() {
#if CYD_USB_VERSION == 2
    if (nvsGetInt("cal_ver", -1) >= CURRENT_CAL_VER) {
        s_madctl = (uint8_t)nvsGetInt("madctl", 0x80); return;
    }
    s_madctl = madctlForCombo(0);
    digitalWrite(TFT_BL, HIGH);
    auto draw = [&]() {
        disp->fillScreen(COL_BG); applyRotation(); disp->fillScreen(COL_BG);
        disp->fillTriangle(2,2,60,2,2,60,COL_AMBER);
        disp->fillTriangle(4,4,56,4,4,56,COL_BG);
        disp->fillTriangle(2,2,60,2,2,60,COL_AMBER);
        disp->fillRect(SCREEN_W-50,2,48,8,g_themeColor);
        disp->fillRect(SCREEN_W-8,2,6,48,g_themeColor);
        disp->fillCircle(24,SCREEN_H-24,20,COL_AMBER);
        disp->fillCircle(24,SCREEN_H-24,16,COL_BG);
        disp->fillCircle(24,SCREEN_H-24,20,COL_AMBER);
        disp->drawLine(SCREEN_W-40,SCREEN_H-24,SCREEN_W-8,SCREEN_H-24,g_themeColor);
        disp->drawLine(SCREEN_W-24,SCREEN_H-40,SCREEN_W-24,SCREEN_H-8,g_themeColor);
        disp->drawCircle(SCREEN_W-24,SCREEN_H-24,14,g_themeColor);
        disp->fillRect(SCREEN_W/2-16,SCREEN_H/2-24,32,6,COL_WHITE);
        disp->fillRect(SCREEN_W/2-4,SCREEN_H/2-24,8,48,COL_WHITE);
        int idx; char buf[16];
        if(s_madctl==(TFT_MAD_MV|TFT_MAD_BGR)) idx=0;
        else if(s_madctl==(TFT_MAD_MV|TFT_MAD_MY|TFT_MAD_BGR)) idx=1;
        else if(s_madctl==0x00) idx=2; else idx=3;
        disp->setTextFont(4); disp->setTextColor(g_themeColor,COL_BG);
        snprintf(buf,sizeof(buf),"MODE %d",idx);
        int tw=disp->textWidth(buf); disp->setCursor((SCREEN_W-tw)/2,68); disp->print(buf);
        disp->setTextFont(2); disp->setTextColor(COL_WHITE,COL_BG);
        const char *m="Tap to change"; tw=disp->textWidth(m);
        disp->setCursor((SCREEN_W-tw)/2,SCREEN_H-72); disp->print(m);
        disp->setTextFont(1); disp->setTextColor(COL_DIM_GRAY,COL_BG);
        m="Hold 2s to confirm"; tw=disp->textWidth(m);
        disp->setCursor((SCREEN_W-tw)/2,SCREEN_H-52); disp->print(m);
    };
    draw();
    unsigned long hs=0; bool wt=false; int cc=0;
    while(true){
        bool nt=touchIsHeld();
        if(nt&&!wt){hs=millis();}
        else if(!nt&&wt&&hs>0){if(millis()-hs<1200){cc=(cc+1)&3;s_madctl=madctlForCombo(cc);draw();}}
        if(nt&&wt&&hs>0&&millis()-hs>=2000)break;
        wt=nt; delay(30);
    }
    while(touchIsHeld())delay(30); delay(200);
    nvsPutInt("madctl", s_madctl);
#endif
}

static void touchCalibrate() {
#if CYD_USB_VERSION == 2
    if(nvsGetInt("cal_ver",-1)>=CURRENT_CAL_VER)return;
    digitalWrite(TFT_BL,HIGH);
    auto draw=[&](){
        disp->fillScreen(COL_BG);
        disp->setTextFont(4); disp->setTextColor(g_themeColor,COL_BG);
        char buf[4]; snprintf(buf,sizeof(buf),"%d",touchGetRotation());
        int tw=disp->textWidth(buf); disp->setCursor((SCREEN_W-tw)/2,SCREEN_H/2-40); disp->print(buf);
        disp->setTextFont(2); disp->setTextColor(COL_WHITE,COL_BG);
        const char *m="Tap to cycle touch"; tw=disp->textWidth(m);
        disp->setCursor((SCREEN_W-tw)/2,SCREEN_H/2); disp->print(m);
        disp->setTextFont(1); disp->setTextColor(COL_DIM_GRAY,COL_BG);
        m="Hold 2s to confirm"; tw=disp->textWidth(m);
        disp->setCursor((SCREEN_W-tw)/2,SCREEN_H/2+30); disp->print(m);
        const int CX=14,CY=14,CS=18; uint16_t tc=COL_DIM_GRAY;
        auto dc=[&](int x,int y){disp->drawRect(x,y,CS,CS,tc);
            disp->drawLine(x,y,x+CS,y+CS,tc); disp->drawLine(x,y+CS,x+CS,y,tc);};
        dc(CX,CY);dc(SCREEN_W-CX-CS,CY);dc(CX,SCREEN_H-CY-CS);dc(SCREEN_W-CX-CS,SCREEN_H-CY-CS);
    };
    draw();
    unsigned long hs=0; bool wt=false; int cx=-1,cy=-1,lx=-1,ly=-1; bool dirty=false;
    while(true){
        int16_t tx,ty; bool nt=touchIsHeld(&tx,&ty);
        if(nt){cx=tx;cy=ty;}
        if(nt&&!wt){hs=millis();lx=cx;ly=cy;dirty=true;}
        else if(!nt&&wt&&hs>0){if(millis()-hs<1200){touchSetRotation((touchGetRotation()+1)%4);draw();}
            if(lx>=0)disp->fillCircle(lx,ly,7,COL_BG);lx=ly=-1;dirty=false;}
        else if(nt&&wt&&hs>0&&millis()-hs>=2000){if(lx>=0)disp->fillCircle(lx,ly,7,COL_BG);break;}
        if(nt&&dirty&&(cx!=lx||cy!=ly)){if(lx>=0)disp->fillCircle(lx,ly,7,COL_BG);
            disp->fillCircle(cx,cy,6,COL_AMBER);disp->drawCircle(cx,cy,6,COL_WHITE);lx=cx;ly=cy;}
        wt=nt;delay(30);
    }
    while(touchIsHeld())delay(30);delay(200);
    nvsPutInt("cal_ver",CURRENT_CAL_VER); nvsPutInt("touch_cal",1);
#endif
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Setup
// ═══════════════════════════════════════════════════════════════════════════════

void setup() {
    Serial.begin(115200);
    nvsInit();
    themeColorInit();

    tft.init();
    applyRotation();
    disp->fillScreen(COL_BG);

    touchInit();
    displayCalibrate();
    applyRotation();
    touchCalibrate();

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    // ── Splash ──
    showSplash();
    delay(4000);

    // ── Init BT dual-mode ──
    showSplash("Starting BT...");
    if (!btScannerInit()) {
        showSplash("BT init FAILED!");
        delay(3000);
    }
    btScannerStart();

    // ── NO sleep timer — always on ──
    s_needsRedraw = true;
    Serial.println("READY");
    Serial.println("xXCYD-BLE-BOT-SCANXx ready");
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Loop
// ═══════════════════════════════════════════════════════════════════════════════

void loop() {
    // ── Scanner update (every 500ms) ──
    static unsigned long lastScanUpdate = 0;
    if (millis() - lastScanUpdate > 500) {
        btScannerUpdate();
        lastScanUpdate = millis();
    }

    // ── Redraw only when device count or alert count changes ──
    int curCount = btScannerGetDeviceCount();
    int curAlerts = btScannerGetAlertCount();
    if (curCount != s_lastDeviceCount || curAlerts != s_lastAlertCount) {
        s_needsRedraw = true;
        s_lastDeviceCount = curCount;
        s_lastAlertCount = curAlerts;
    }
    // No periodic forced redraw — avoids flicker. RSSI bars update on the next
    // count change or button tap, which is frequent enough when scanning.

    // ── Serial ──
    if (Serial.available()) {
        int cmd = Serial.read();
        if (cmd == 'R' || cmd == 'r') { Serial.println("READY"); }
        else if (cmd == 'S' || cmd == 's') {
            // Capture via 8-bit sprite (matches CYD-Poker/Weather proven method)
            TFT_eSprite spr(&tft);
            spr.setColorDepth(8);
            uint8_t *fb = (uint8_t*)spr.createSprite(SCREEN_W, SCREEN_H);
            if (fb) {
                screenListDraw(spr);
                Serial.print("RGB332:");
                Serial.write(fb, SCREEN_W * SCREEN_H);
                Serial.flush();
                spr.deleteSprite();
            } else {
                Serial.println("OOM: sprite alloc failed");
            }
        }
#if CYD_USB_VERSION == 2
        else if (cmd == 'M' || cmd == 'm') {
            int cur=3;
            if(s_madctl==(TFT_MAD_MV|TFT_MAD_BGR))cur=0;
            else if(s_madctl==(TFT_MAD_MV|TFT_MAD_MY|TFT_MAD_BGR))cur=1;
            else if(s_madctl==0x00)cur=2;
            cur=(cur+1)&3; s_madctl=madctlForCombo(cur);
            nvsPutInt("madctl",s_madctl); nvsPutInt("cal_ver",CURRENT_CAL_VER);
            applyRotation(); Serial.print("MADCTL_MODE:"); Serial.println(cur);
            s_needsRedraw=true;
        } else if (cmd == 'T' || cmd == 't') {
            touchSetRotation((touchGetRotation()+1)%4); nvsPutInt("touch_cal",1);
            Serial.print("TOUCH_ROT:"); Serial.println(touchGetRotation());
        }
#endif
    }

    // ── Touch ──
    TouchEvent evt = touchPoll();
    if (evt.tap == TapEvent::Tap) {
        int tx = evt.tapX, ty = evt.tapY;

        if (s_backlightOff) {
            s_backlightOff = false;
            digitalWrite(TFT_BL, HIGH);
            s_needsRedraw = true;
            delay(30);
            goto redraw;
        }

        // Power ring (top-right corner)
        if (screenListHitPower(tx, ty)) { goToSleep(); return; }

        // Shield button (bottom bar)
        if (screenListHitShield(tx, ty)) {
            btScannerSetShieldUp(!btScannerGetShieldUp());
            nvsPutInt("shield_up", btScannerGetShieldUp() ? 1 : 0);
            s_needsRedraw = true;
        }

        // Sort button (bottom bar)
        if (screenListHitSort(tx, ty)) {
            int cur = (int)btScannerGetSortMode();
            cur = (cur + 1) % 4;
            btScannerSetSortMode((SortMode)cur);
            nvsPutInt("sort_mode", cur);
            s_needsRedraw = true;
        }

        // Scan toggle button (bottom bar — STOP/SCAN)
        if (screenListHitScan(tx, ty)) {
            btScannerToggleScan();
            s_needsRedraw = true;
        }

        // Theme dot (lower-right corner) — cycle through all 9 colors
        if (screenListHitTheme(tx, ty)) {
            themeColorSet((themeColorGetIdx() + 1) % THEME_COUNT);
            nvsPutInt("theme_idx", themeColorGetIdx());
            s_needsRedraw = true;
        }

    }

redraw:
    if (s_needsRedraw) {
        screenListDraw(tft);
        s_needsRedraw = false;
    }

    delay(20);
}
