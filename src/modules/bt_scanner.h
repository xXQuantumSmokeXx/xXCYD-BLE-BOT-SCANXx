#pragma once
#include <cstdint>
#include "../config/config.h"

// ── Enums ─────────────────────────────────────────────────────────────────────

enum class BTSource : uint8_t { BLE, Classic, Both };
enum class BTDeviceType : uint8_t {
    Unknown, Phone, Computer, Headset, AudioDevice,
    Wearable, Peripheral, Toy, Health, Vehicle, NetworkDevice, InputDevice
};

// ── Device entry ─────────────────────────────────────────────────────────────

struct BTDeviceEntry {
    uint8_t  mac[BT_MAC_LEN];
    char     macStr[BT_MAC_STR_LEN];     // "AA:BB:CC:DD:EE:FF"
    char     name[BT_NAME_LEN];          // device name (empty if not advertised)
    int8_t   rssi;                       // -127 to 0 dBm

    // BLE-specific
    int8_t   txPower;                    // 127 = unknown
    uint16_t appearance;
    uint16_t manufacturerId;             // Bluetooth SIG company ID
    bool     hasTxPower;
    bool     hasAppearance;
    bool     hasMfgData;

    // Classic BT-specific
    uint32_t    classOfDevice;           // 24-bit CoD
    BTDeviceType deviceType;

    // Common
    char        manufacturer[BT_MFG_NAME_LEN];  // OUI lookup result
    BTSource    source;
    unsigned long firstSeenMs;
    unsigned long lastSeenMs;
    uint8_t     age;                     // radar frames since last seen
    uint8_t     scanCount;
    int         polarX, polarY;          // cached radar position

    // Shield/alert
    bool        isAlert;
};

// ── Scan state ───────────────────────────────────────────────────────────────

enum class ScanState : uint8_t {
    Idle,
    BLEScanning,
    ClassicInquiry,
    Paused
};

// ── Sort modes ───────────────────────────────────────────────────────────────

enum class SortMode : uint8_t { RSSI, Name, DeviceType, FirstSeen };

// ── API ──────────────────────────────────────────────────────────────────────

// Initialize Bluetooth stack (dual-mode: BLE + Classic)
// Returns true on success
bool btScannerInit();

// Start scanning (called after init, or after stop)
void btScannerStart();

// Stop all scanning
void btScannerStop();

// Toggle scan on/off (for the SCAN/STOP button)
void btScannerToggleScan();

// Periodic update — drain BLE queue, process Classic results, age devices
// Call from main loop every ~500ms
void btScannerUpdate();

// ── Device access (read-only from UI thread) ─────────────────────────────────

int          btScannerGetDeviceCount();
const BTDeviceEntry* btScannerGetDevices();       // returns sorted array
const BTDeviceEntry* btScannerGetDevice(int idx); // nullptr if out of range

// ── Config ───────────────────────────────────────────────────────────────────

ScanState btScannerGetState();

void btScannerSetRssiFilter(int8_t minRssi);       // -127 to 0, or -127 = off
int8_t btScannerGetRssiFilter();

void btScannerSetSortMode(SortMode mode);
SortMode btScannerGetSortMode();

void btScannerSetClassicEnabled(bool on);
bool btScannerGetClassicEnabled();

// ── Shield / Alert ───────────────────────────────────────────────────────────

void btScannerSetShieldUp(bool up);
bool btScannerGetShieldUp();
int  btScannerGetAlertCount();
void btScannerClearAlerts();

// ── Device management ────────────────────────────────────────────────────────

void btScannerClearDevices();       // wipe all, restart fresh
bool btScannerDeleteDevice(int idx); // remove one device

// ── History ───────────────────────────────────────────────────────────────────

int  btScannerGetHistoryPoint(int idx);  // 0 = newest, returns device count at that point
void btScannerGetHistoryRaw(int *buf, int maxLen);  // fill buf with HISTORY_POINTS values

// ── Utilities ────────────────────────────────────────────────────────────────

const char* btDeviceTypeName(BTDeviceType t);
const char* btSourceLabel(BTSource s);
