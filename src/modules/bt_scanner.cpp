#include "bt_scanner.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_bt_device.h>
#include <esp_gap_bt_api.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <cstring>
#include <cstdio>
#include <algorithm>

// ═══════════════════════════════════════════════════════════════════════════════
//  Constants
// ═══════════════════════════════════════════════════════════════════════════════

#define BLE_QUEUE_SIZE     64     // entries in the BLE -> main loop queue (doubled: scan responses now delivered)
// 0 = silence per-advertisement logging on the serial monitor
#define BLE_DEBUG_ADV      0      // 0 = quiet; 1 logs every advertisement
#define MAX_CLASSIC_RESULTS 32    // per inquiry cycle
#define BT_DUAL_MODE_ATTEMPT 1    // bring the controller up as BTDM before BLE; 0 = BLE only

// ═══════════════════════════════════════════════════════════════════════════════
//  OUI Manufacturer Lookup Table
// ═══════════════════════════════════════════════════════════════════════════════

// OUI vendor table: generated from the IEEE registry by tools/gen-oui.mjs.
// 16k prefixes across ~200 vendors; binary search keeps the lookup cheap.
#include "oui_table.h"
#include "sd_log.h"
#include "signature_match.h"

static const char* lookupOUI(const char* macStr) {
    // macStr is "AA:BB:CC:DD:EE:FF"; the key is the first three octets.
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    const uint32_t key = ((uint32_t)nib(macStr[0]) << 20) | ((uint32_t)nib(macStr[1]) << 16)
                       | ((uint32_t)nib(macStr[3]) << 12) | ((uint32_t)nib(macStr[4]) << 8)
                       | ((uint32_t)nib(macStr[6]) << 4)  |  (uint32_t)nib(macStr[7]);
    int lo = 0;
    int hi = OUI_ENTRY_COUNT - 1;
    while (lo <= hi) {
        const int mid = lo + (hi - lo) / 2;
        const OuiRec &r = OUI_TABLE[mid];
        const uint32_t k = ((uint32_t)r.b[0] << 16) | ((uint32_t)r.b[1] << 8) | (uint32_t)r.b[2];
        if (k == key) return OUI_VENDORS[r.v];
        if (k < key) lo = mid + 1; else hi = mid - 1;
    }
    return "";  // no OUI match; callers fall back to the MAC tail
}
// SIG company identifiers carried in manufacturer data. Devices with a random MAC
// (phones, beacons) often have no usable OUI, but still advertise a company id.
// Bluetooth SIG company identifiers: generated table, binary searched.
#include "company_table.h"

static const char* lookupCompanyId(uint16_t id) {
    int lo = 0;
    int hi = CID_COUNT - 1;
    while (lo <= hi) {
        const int mid = lo + (hi - lo) / 2;
        const uint16_t v = CID_IDS[mid];
        if (v == id) return CID_NAMES[CID_NAME_IDX[mid]];
        if (v < id) lo = mid + 1; else hi = mid - 1;
    }
    return "";
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Class of Device Decoder
// ═══════════════════════════════════════════════════════════════════════════════

const char* btDeviceTypeName(BTDeviceType t) {
    switch (t) {
        case BTDeviceType::Phone:        return "Phone";
        case BTDeviceType::Computer:     return "Computer";
        case BTDeviceType::Headset:      return "Headset";
        case BTDeviceType::AudioDevice:  return "Audio";
        case BTDeviceType::Wearable:     return "Wearable";
        case BTDeviceType::Peripheral:   return "Peripheral";
        case BTDeviceType::Toy:          return "Toy";
        case BTDeviceType::Health:       return "Health";
        case BTDeviceType::Vehicle:      return "Vehicle";
        case BTDeviceType::NetworkDevice:return "Network";
        case BTDeviceType::InputDevice:  return "Input";
        default:                         return "Unknown";
    }
}

const char* btSourceLabel(BTSource s) {
    switch (s) {
        case BTSource::BLE:     return "BLE";
        case BTSource::Classic: return "CL";
        case BTSource::Both:    return "Both";
        default:                return "?";
    }
}

static BTDeviceType decodeClassOfDevice(uint32_t cod) {
    // CoD is 24-bit: [11:8]=major service, [7:2]=major device, [1:0]=format
    uint8_t majorDevice = (cod >> 2) & 0x1F;
    // Major device class values from Bluetooth SIG
    switch (majorDevice) {
        case 1:  return BTDeviceType::Computer;
        case 2:  return BTDeviceType::Phone;
        case 3:  return BTDeviceType::NetworkDevice;
        case 4:  return BTDeviceType::AudioDevice;
        case 5:  return BTDeviceType::Peripheral;
        case 6:  return BTDeviceType::Peripheral;  // Imaging
        case 7:  return BTDeviceType::Wearable;
        case 8:  return BTDeviceType::Toy;
        case 9:  return BTDeviceType::Health;
        case 31: return BTDeviceType::InputDevice;  // Uncategorized: keyboard/mouse
        default: {
            // Headset class: major service = audio (bit 10), minor may vary
            if (cod & 0x200000) return BTDeviceType::Headset;
            if (cod & 0x40000)  return BTDeviceType::AudioDevice;
            return BTDeviceType::Unknown;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Forward declarations (used by upsertDevice before full definition)
// ═══════════════════════════════════════════════════════════════════════════════

static bool s_shieldUp = false;
static int  s_alertCount = 0;

// ═══════════════════════════════════════════════════════════════════════════════
//  Device Database
// ═══════════════════════════════════════════════════════════════════════════════

static BTDeviceEntry s_devices[BT_DEVICE_MAX];
static int s_deviceCount = 0;

// ── Find device by MAC ──
static int findDeviceByMac(const uint8_t mac[BT_MAC_LEN]) {
    for (int i = 0; i < s_deviceCount; i++) {
        if (memcmp(s_devices[i].mac, mac, BT_MAC_LEN) == 0) return i;
    }
    return -1;
}

// ── Format MAC string ──
static void formatMacStr(char *buf, const uint8_t mac[BT_MAC_LEN]) {
    snprintf(buf, BT_MAC_STR_LEN, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// ── Add or update device ──
static BTDeviceEntry* upsertDevice(const uint8_t mac[BT_MAC_LEN], BTSource source) {
    int idx = findDeviceByMac(mac);
    if (idx >= 0) {
        // Update source if richer
        if (s_devices[idx].source == BTSource::BLE && source == BTSource::Classic)
            s_devices[idx].source = BTSource::Both;
        else if (s_devices[idx].source == BTSource::Classic && source == BTSource::BLE)
            s_devices[idx].source = BTSource::Both;
        s_devices[idx].lastSeenMs = millis();
        s_devices[idx].age = 0;
        s_devices[idx].scanCount++;
        return &s_devices[idx];
    }

    // New device
    if (s_deviceCount >= BT_DEVICE_MAX) {
        // LRU eviction — find oldest lastSeenMs
        int oldest = 0;
        for (int i = 1; i < s_deviceCount; i++) {
            if (s_devices[i].lastSeenMs < s_devices[oldest].lastSeenMs)
                oldest = i;
        }
        // Remove oldest
        if (oldest < s_deviceCount - 1)
            memmove(&s_devices[oldest], &s_devices[oldest + 1],
                    (s_deviceCount - oldest - 1) * sizeof(BTDeviceEntry));
        s_deviceCount--;
    }

    // Init new entry
    BTDeviceEntry *entry = &s_devices[s_deviceCount];
    memset(entry, 0, sizeof(BTDeviceEntry));
    memcpy(entry->mac, mac, BT_MAC_LEN);
    formatMacStr(entry->macStr, mac);
    entry->rssi = -127;
    entry->txPower = 127;
    entry->source = source;
    entry->firstSeenMs = millis();
    entry->lastSeenMs = millis();
    entry->deviceType = BTDeviceType::Unknown;
    entry->age = 0;
    entry->scanCount = 1;

    // OUI lookup
    strncpy(entry->manufacturer, lookupOUI(entry->macStr), BT_MFG_NAME_LEN - 1);

    // Shield check
    if (s_shieldUp) {
        entry->isAlert = true;
        s_alertCount++;
    }

    s_deviceCount++;
    return entry;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  BLE Scan
// ═══════════════════════════════════════════════════════════════════════════════

// Minimal data passed from BLE callback to main loop
struct BLEDeviceData {
    uint8_t mac[BT_MAC_LEN];
    char    name[BT_NAME_LEN];
    int8_t  rssi;
    int8_t  txPower;
    uint16_t appearance;
    uint16_t manufacturerId;
    bool    hasName;
    bool    hasTxPower;
    bool    hasAppearance;
    bool    hasMfgData;
    bool    hadNonAsciiName;
    uint16_t svc16[4];      // 16-bit service UUIDs seen in the advertisement
    uint8_t  svcCount;
    bool     has128Svc;     // any 128-bit service UUID present
    uint8_t  uuid128[16];   // first 128-bit service UUID seen
    uint8_t  payload[62];   // raw ADV_IND + SCAN_RSP bytes, for name-parse debugging
    uint8_t  payloadLen;
    uint8_t  addrType;      // 0 = public, else random
};

static QueueHandle_t s_bleQueue = nullptr;

// Advertisement counters: drive the serial summary and the on-screen diagnostic.
static uint32_t s_advSeen = 0;
static uint32_t s_advNamed = 0;

class BleScanCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        BLEDeviceData data;
        memset(&data, 0, sizeof(data));

        // MAC
        std::string macStr = advertisedDevice.getAddress().toString();
        const char *p = macStr.c_str();
        for (int i = 0; i < BT_MAC_LEN; i++) {
            char byteStr[3] = {p[i * 3], p[i * 3 + 1], '\0'};
            data.mac[i] = (uint8_t)strtol(byteStr, nullptr, 16);
        }

        // Name — try both complete and short local name
        if (advertisedDevice.haveName()) {
            std::string name = advertisedDevice.getName();
            if (!name.empty()) {
                strncpy(data.name, name.c_str(), BT_NAME_LEN - 1);
                data.hasName = true;
            }
        }
        // Fallback: manually parse advertising payload for name
        if (!data.hasName) {
            uint8_t* payload = advertisedDevice.getPayload();
            size_t payloadLen = advertisedDevice.getPayloadLength();
            size_t pos = 0;
            while (pos + 1 < payloadLen) {
                uint8_t fieldLen = payload[pos];
                if (fieldLen == 0 || pos + fieldLen >= payloadLen) break;
                uint8_t fieldType = payload[pos + 1];
                // 0x09 = Complete Local Name, 0x08 = Shortened Local Name
                if (fieldType == 0x09 || fieldType == 0x08) {
                    size_t nameLen = fieldLen - 1;
                    if (nameLen > BT_NAME_LEN - 1) nameLen = BT_NAME_LEN - 1;
                    memcpy(data.name, &payload[pos + 2], nameLen);
                    data.hasName = true;
                    break;
                }
                pos += fieldLen + 1;
            }
        }

        // Names can contain UTF-8 (emoji, CJK) that the ASCII-only TFT font cannot
        // draw, which shows up as junk glyphs. Keep printable ASCII; if nothing
        // survives, clear the name so the UI falls back to the MAC address.
        if (data.hasName) {
            char clean[BT_NAME_LEN];
            size_t w = 0;
            for (size_t i = 0; data.name[i] && w < BT_NAME_LEN - 1; i++) {
                unsigned char c = (unsigned char)data.name[i];
                if (c < 0x20 || c >= 0x7F) continue;   // drop non-printable / non-ASCII
                if (w == 0 && c == ' ') continue;      // no leading space
                clean[w++] = (char)c;
            }
            while (w > 0 && clean[w - 1] == ' ') w--;  // no trailing space
            clean[w] = (char)0;
            if (w > 0) {
                memcpy(data.name, clean, w + 1);
            } else {
                // Keep the fact that a name existed, so the UI can show MAC* instead
                // of pretending the device never advertised one.
                data.name[0] = (char)0;
                data.hasName = false;
                data.hadNonAsciiName = true;
            }
        }

        // RSSI
        data.rssi = advertisedDevice.getRSSI();
        data.addrType = (uint8_t)advertisedDevice.getAddressType();

        // TX Power
        if (advertisedDevice.haveTXPower()) {
            data.txPower = advertisedDevice.getTXPower();
            data.hasTxPower = true;
        } else {
            data.txPower = 127;
        }

        // Appearance
        if (advertisedDevice.haveAppearance()) {
            data.appearance = advertisedDevice.getAppearance();
            data.hasAppearance = true;
        }

        // Manufacturer data
        if (advertisedDevice.haveManufacturerData()) {
            std::string mfgData = advertisedDevice.getManufacturerData();
            if (mfgData.length() >= 2) {
                data.manufacturerId = (uint8_t)mfgData[0] | ((uint8_t)mfgData[1] << 8);
                data.hasMfgData = true;
            }
        }

        // Service UUIDs: keep the 16-bit aliases, flag any 128-bit ones. This is the
        // cheapest fingerprint for ecosystems (Fast Pair, vendor services).
        data.svcCount = 0;
        data.has128Svc = false;
        {
            const size_t svcTotal = advertisedDevice.getServiceUUIDCount();
            for (size_t i = 0; i < svcTotal && i < 8; i++) {
                const std::string u = advertisedDevice.getServiceUUID(i).toString();
                // "0000fe2c-0000-1000-8000-00805f9b34fb" carries a 16-bit alias
                if (u.size() == 36 && u.compare(0, 4, "0000") == 0 &&
                    u.compare(8, 28, "-0000-1000-8000-00805f9b34fb") == 0) {
                    if (data.svcCount < 4)
                        data.svc16[data.svcCount++] = (uint16_t)strtol(u.substr(4, 4).c_str(), nullptr, 16);
                } else {
                    // Keep the first 128-bit UUID: vendor-specific services are the
                    // strongest fingerprints. Parsed from the string form so this does
                    // not depend on the IDF uuid struct layout.
                    if (!data.has128Svc && u.size() == 36) {
                        int b = 0;
                        int hi = -1;
                        for (size_t k = 0; k < u.size(); k++) {
                            const char ch = u[k];
                            int v = -1;
                            if (ch >= '0' && ch <= '9') v = ch - '0';
                            else if (ch >= 'a' && ch <= 'f') v = ch - 'a' + 10;
                            else if (ch >= 'A' && ch <= 'F') v = ch - 'A' + 10;
                            if (v < 0) continue;                 // skip the '-'
                            if (hi < 0) hi = v;
                            else { if (b < 16) data.uuid128[b++] = (uint8_t)((hi << 4) | v); hi = -1; }
                        }
                    }
                    data.has128Svc = true;
                }
            }
        }
        // Keep the raw advertisement so name parsing can be checked off-device.
        data.payloadLen = (uint8_t)advertisedDevice.getPayloadLength();
        if (data.payloadLen > sizeof(data.payload)) data.payloadLen = sizeof(data.payload);
        if (data.payloadLen) memcpy(data.payload, advertisedDevice.getPayload(), data.payloadLen);
        if (data.payloadLen) memset(data.payload + data.payloadLen, 0, sizeof(data.payload) - data.payloadLen);
        else memset(data.payload, 0, sizeof(data.payload));
        // Push to queue — non-blocking, drop if full
        // Counters always run; printing stays behind the macro.
        s_advSeen++;
        if (data.hasName) s_advNamed++;

#if BLE_DEBUG_ADV
        if (data.hasName) {
            Serial.printf("ADV %s rssi=%d len=%u name=%s\n",
                          macStr.c_str(), (int)advertisedDevice.getRSSI(),
                          (unsigned)advertisedDevice.getPayloadLength(), data.name);
        } else if ((s_advSeen % 200u) == 0u) {
            Serial.printf("ADV summary: %u reports, %u named\n", (unsigned)s_advSeen, (unsigned)s_advNamed);
        }
#endif
        xQueueSend(s_bleQueue, &data, 0);
    }
};

static BLEScan *s_pBLEScan = nullptr;
static bool s_dualModeUp = false;
bool g_captureJson = true;   // serial JSON dump of new devices; "J" toggles
bool g_rawDump    = true;   // hex dump of each new device advertisement; "H" toggles

// ═══════════════════════════════════════════════════════════════════════════════
//  Classic BT Inquiry
// ═══════════════════════════════════════════════════════════════════════════════

static bool s_classicEnabled = true;

struct ClassicBTDevice {
    uint8_t mac[BT_MAC_LEN];
    char    name[BT_NAME_LEN];
    int8_t  rssi;
    uint32_t classOfDevice;
    bool    hasName;
};

static ClassicBTDevice s_classicResults[MAX_CLASSIC_RESULTS];
static int s_classicResultCount = 0;
static bool s_classicInProgress = false;

static void classicBTCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    switch (event) {
        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
            if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
                s_classicInProgress = true;
                s_classicResultCount = 0;
            } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
                s_classicInProgress = false;
            }
            break;

        case ESP_BT_GAP_DISC_RES_EVT: {
            if (s_classicResultCount >= MAX_CLASSIC_RESULTS) break;
            ClassicBTDevice *dev = &s_classicResults[s_classicResultCount];
            memset(dev, 0, sizeof(ClassicBTDevice));

            memcpy(dev->mac, param->disc_res.bda, BT_MAC_LEN);
            dev->rssi = -127;  // default, will be updated from properties

            // Iterate discovery result properties
            for (int propIdx = 0; propIdx < param->disc_res.num_prop; propIdx++) {
                esp_bt_gap_dev_prop_t *prop = &param->disc_res.prop[propIdx];
                uint8_t *pval = (uint8_t*)prop->val;  // val is void*
                switch (prop->type) {
                    case ESP_BT_GAP_DEV_PROP_RSSI:
                        if (prop->len >= 1)
                            dev->rssi = (int8_t)pval[0];
                        break;
                    case ESP_BT_GAP_DEV_PROP_COD:
                        if (prop->len >= 3)
                            dev->classOfDevice = (uint32_t)pval[0]
                                               | ((uint32_t)pval[1] << 8)
                                               | ((uint32_t)pval[2] << 16);
                        break;
                    case ESP_BT_GAP_DEV_PROP_EIR: {
                        // Parse EIR for device name (types 0x08, 0x09)
                        int pos = 0;
                        while (pos < prop->len) {
                            uint8_t fieldLen = pval[pos];
                            if (fieldLen == 0 || pos + fieldLen >= prop->len) break;
                            uint8_t fieldType = pval[pos + 1];
                            if (fieldType == 0x09 || fieldType == 0x08) {
                                int nameLen = fieldLen - 1;
                                if (nameLen > BT_NAME_LEN - 1) nameLen = BT_NAME_LEN - 1;
                                memcpy(dev->name, &pval[pos + 2], nameLen);
                                dev->hasName = true;
                                break;
                            }
                            pos += fieldLen + 1;
                        }
                        break;
                    }
                    default:
                        break;
                }
            }

            s_classicResultCount++;
            break;
        }

        default:
            break;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Scan Scheduler
// ═══════════════════════════════════════════════════════════════════════════════

static ScanState  s_scanState = ScanState::Idle;
static unsigned long s_lastClassicInquiry = 0;
static unsigned long s_bleScanStart = 0;
static bool s_bleActive = false;

static void bleStartScan() {
    if (!s_pBLEScan) return;
    s_pBLEScan->start(0, nullptr, false);  // continuous scan
    s_bleActive = true;
    s_scanState = ScanState::BLEScanning;
}

static void bleStopScan() {
    if (!s_pBLEScan || !s_bleActive) return;
    s_pBLEScan->stop();
    s_bleActive = false;
    if (s_scanState == ScanState::BLEScanning) s_scanState = ScanState::Idle;
}

static void classicStartInquiry() {
    if (!s_classicEnabled) return;
    s_scanState = ScanState::ClassicInquiry;
    s_classicResultCount = 0;
    s_lastClassicInquiry = millis();
    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY,
                                CLASSIC_INQUIRY_SEC, 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Sort
// ═══════════════════════════════════════════════════════════════════════════════

static SortMode s_sortMode = SortMode::RSSI;

static int cmpRssi(const void *a, const void *b) {
    return ((const BTDeviceEntry*)b)->rssi - ((const BTDeviceEntry*)a)->rssi;
}

static int cmpName(const void *a, const void *b) {
    auto *da = (const BTDeviceEntry*)a;
    auto *db = (const BTDeviceEntry*)b;
    bool na = (da->name[0] != '\0'), nb = (db->name[0] != '\0');
    if (na && !nb) return -1;
    if (!na && nb) return 1;
    if (!na && !nb) return strcmp(da->macStr, db->macStr);
    return strcasecmp(da->name, db->name);
}

static int cmpFirstSeen(const void *a, const void *b) {
    auto *da = (const BTDeviceEntry*)a;
    auto *db = (const BTDeviceEntry*)b;
    if (da->firstSeenMs < db->firstSeenMs) return 1;
    if (da->firstSeenMs > db->firstSeenMs) return -1;
    return 0;
}

static int cmpType(const void *a, const void *b) {
    auto *da = (const BTDeviceEntry*)a;
    auto *db = (const BTDeviceEntry*)b;
    if (da->deviceType != db->deviceType)
        return (int)da->deviceType - (int)db->deviceType;
    return da->rssi - db->rssi;
}

static void sortDevices() {
    switch (s_sortMode) {
        case SortMode::RSSI:      qsort(s_devices, s_deviceCount, sizeof(BTDeviceEntry), cmpRssi); break;
        case SortMode::Name:      qsort(s_devices, s_deviceCount, sizeof(BTDeviceEntry), cmpName); break;
        case SortMode::FirstSeen: qsort(s_devices, s_deviceCount, sizeof(BTDeviceEntry), cmpFirstSeen); break;
        case SortMode::DeviceType: qsort(s_devices, s_deviceCount, sizeof(BTDeviceEntry), cmpType); break;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Shield / Alert
// ═══════════════════════════════════════════════════════════════════════════════

static uint8_t s_knownMacs[BT_DEVICE_MAX][BT_MAC_LEN];  // snapshot when shields go up
static int  s_knownCount = 0;

static bool isKnownDevice(const uint8_t mac[BT_MAC_LEN]) {
    for (int i = 0; i < s_knownCount; i++) {
        if (memcmp(s_knownMacs[i], mac, BT_MAC_LEN) == 0) return true;
    }
    return false;
}

static void snapshotKnownDevices() {
    s_knownCount = 0;
    for (int i = 0; i < s_deviceCount && s_knownCount < BT_DEVICE_MAX; i++) {
        memcpy(s_knownMacs[s_knownCount], s_devices[i].mac, BT_MAC_LEN);
        s_knownCount++;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  History
// ═══════════════════════════════════════════════════════════════════════════════

static int  s_history[HISTORY_POINTS] = {0};
static int  s_historyIdx = 0;
static unsigned long s_lastHistoryUpdate = 0;

static void updateHistory() {
    if (millis() - s_lastHistoryUpdate < 60000) return;  // every 60s
    s_lastHistoryUpdate = millis();
    s_history[s_historyIdx] = s_deviceCount;
    s_historyIdx = (s_historyIdx + 1) % HISTORY_POINTS;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  RSSI Filter
// ═══════════════════════════════════════════════════════════════════════════════

static int8_t s_rssiFilter = RSSI_FILTER_DEFAULT;

static bool passesRssiFilter(int8_t rssi) {
    return rssi >= s_rssiFilter;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Public API
// ═══════════════════════════════════════════════════════════════════════════════

bool btScannerInit() {
    Serial.println("BT: starting BLE init...");

    // ── Init BLE via Arduino library (standard path, no dual-mode tricks) ──
#if BT_DUAL_MODE_ATTEMPT
    // Bring the controller up as BTDM (BR/EDR + BLE) *before* the Arduino BLE layer
    // runs. BLEDevice::init() enables BLE-only, and a later attempt to switch with
    // esp_bt_controller_enable(ESP_BT_MODE_BTDM) fails with ESP_ERR_INVALID_STATE -
    // which is why Classic inquiry never worked on this board.
    {
        esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        if (esp_bt_controller_init(&cfg) == ESP_OK) {
            if (esp_bt_controller_enable(ESP_BT_MODE_BTDM) == ESP_OK) {
                s_dualModeUp = true;
            } else {
                esp_bt_controller_deinit();
            }
        }
    }
    Serial.printf("BT: dual-mode controller %s\n", s_dualModeUp ? "up" : "unavailable");
#endif
    BLEDevice::init("CYD-BLE-BOT-SCAN");

    s_pBLEScan = BLEDevice::getScan();
    // wantDuplicates=true is REQUIRED: with the default (false) the stack drops the
    // scan-response report as a "duplicate" of the advertisement, so haveName() is
    // false for every device whose name lives in the scan response.
    s_pBLEScan->setAdvertisedDeviceCallbacks(new BleScanCallbacks(), true);
    s_pBLEScan->setActiveScan(true);
    s_pBLEScan->setInterval(BLE_SCAN_INTERVAL_MS);
    s_pBLEScan->setWindow(BLE_SCAN_WINDOW_MS);

    s_bleQueue = xQueueCreate(BLE_QUEUE_SIZE, sizeof(BLEDeviceData));

    Serial.println("BT: BLE stack OK");

    // ── Attempt Classic BT (BR/EDR) — may not work on all ESP32 BLE stacks ──
    // The controller is in BLE-only mode from BLEDevice::init().
    // We try a single-shot upgrade to BTDM — if it fails, we accept BLE-only.
    if (s_dualModeUp) {
        esp_bt_gap_register_callback(classicBTCallback);
        esp_bt_dev_set_device_name("CYD-BLE-BOT-SCAN");
        s_classicEnabled = true;
        Serial.println("BT: Classic inquiry enabled (dual mode)");
    } else {
        s_classicEnabled = false;
        Serial.println("BT: Classic unavailable, BLE-only");
    }

    Serial.println("BT: init complete");
    return true;
}

void btScannerStart() {
    bleStartScan();
}

void btScannerStop() {
    bleStopScan();
    s_scanState = ScanState::Paused;
}

void btScannerToggleScan() {
    if (s_scanState == ScanState::BLEScanning || s_scanState == ScanState::ClassicInquiry) {
        bleStopScan();
        s_scanState = ScanState::Paused;
    } else {
        bleStartScan();
    }
}

// One log line per MAC per boot: entries expire and are recreated, so the device
// table alone cannot remember what has already been written.
#define LOG_MAC_MAX 128
static uint8_t s_loggedMac[LOG_MAC_MAX][BT_MAC_LEN];
static int     s_loggedCount = 0;

static bool wasLogged(const uint8_t mac[BT_MAC_LEN]) {
    for (int i = 0; i < s_loggedCount; i++)
        if (memcmp(s_loggedMac[i], mac, BT_MAC_LEN) == 0) return true;
    return false;
}

static void markLogged(const uint8_t mac[BT_MAC_LEN]) {
    if (s_loggedCount < LOG_MAC_MAX) memcpy(s_loggedMac[s_loggedCount++], mac, BT_MAC_LEN);
}

static void logRawPayload(const BTDeviceEntry *e, const BLEDeviceData &d) {
    if (!g_rawDump || d.payloadLen == 0) return;
    char line[200];
    int n = snprintf(line, sizeof(line), "{\"raw\":\"%s\",\"len\":%u,\"hex\":\"",
                     e->macStr, (unsigned)d.payloadLen);
    if (n < 0) return;
    for (uint8_t i = 0; i < d.payloadLen && n < (int)sizeof(line) - 4; i++)
        n += snprintf(line + n, sizeof(line) - n, "%02X", d.payload[i]);
    snprintf(line + n, sizeof(line) - n, "\"}");
    Serial.print("RAW ");
    Serial.println(line);
    sdLogAppend(line);
}
static void logBleDevice(const BTDeviceEntry *e, const BLEDeviceData &d) {
    char line[320];
    int n = snprintf(line, sizeof(line),
        "{\"mac\":\"%s\",\"rssi\":%d,\"name\":\"%s\",\"vendor\":\"%s\","
        "\"cid\":\"0x%04X\",\"app\":\"0x%04X\",\"tx\":%d,\"addr\":\"%s\",\"sig\":\"%s\",\"svc\":[",
        e->macStr, (int)d.rssi, e->name, e->manufacturer,
        d.hasMfgData ? d.manufacturerId : 0,
        d.hasAppearance ? d.appearance : 0,
        d.hasTxPower ? (int)d.txPower : 127,
        d.addrType == 0 ? "public" : "random", e->sigLabel);
    if (n < 0) return;
    if (n > (int)sizeof(line) - 8) n = (int)sizeof(line) - 8;

    for (uint8_t i = 0; i < d.svcCount && n < (int)sizeof(line) - 12; i++)
        n += snprintf(line + n, sizeof(line) - n, "%s\"0x%04X\"", i ? "," : "", d.svc16[i]);

    n += snprintf(line + n, sizeof(line) - n, "],\"v128\":%s", d.has128Svc ? "true" : "false");
    if (d.has128Svc && n < (int)sizeof(line) - 42) {
        n += snprintf(line + n, sizeof(line) - n, ",\"u128\":\"");
        for (int i = 0; i < 16; i++)
            n += snprintf(line + n, sizeof(line) - n, "%02X", d.uuid128[i]);
        n += snprintf(line + n, sizeof(line) - n, "\"");
    }
    snprintf(line + n, sizeof(line) - n, "}");

    Serial.print("JSON ");
    Serial.println(line);
    sdLogAppend(line);
}
void btScannerUpdate() {
    // -- Drain BLE queue --
    BLEDeviceData data;
    while (xQueueReceive(s_bleQueue, &data, 0) == pdTRUE) {
        if (!passesRssiFilter(data.rssi)) continue;

        BTDeviceEntry *entry = upsertDevice(data.mac, BTSource::BLE);
        if (!entry) continue;

entry->rssi = data.rssi;
        entry->addrType = data.addrType;

        if (data.hasName && data.name[0]) {
            strncpy(entry->name, data.name, BT_NAME_LEN - 1);
        }
        if (data.hadNonAsciiName) entry->hasAdvertisedName = true;
        if (data.hasTxPower) {
            entry->txPower = data.txPower;
            entry->hasTxPower = true;
        }
        if (data.hasAppearance) {
            entry->appearance = data.appearance;
            entry->hasAppearance = true;
        }
        if (data.hasMfgData) {
            entry->manufacturerId = data.manufacturerId;
            entry->hasMfgData = true;
            // Random-MAC devices have no OUI match; the company id still names them.
            if (!entry->manufacturer[0]) {
                const char *c = lookupCompanyId(data.manufacturerId);
                if (c[0]) strncpy(entry->manufacturer, c, BT_MFG_NAME_LEN - 1);
            }
        }

        // Log once, after name/vendor are populated. Devices that never advertise a
        // name are written after a few sightings so they are not lost entirely.
        if (g_captureJson && !entry->logged && !wasLogged(data.mac) &&
            (data.hasName || entry->scanCount >= 3)) {
            logBleDevice(entry, data);
            logRawPayload(entry, data);
            entry->logged = true;
            markLogged(data.mac);
        }
        // Detection signatures: name prefix / company id / service UUIDs. Runs on
        // every packet because the name often arrives after the first advertisement.
        {
            const char *sig = signatureMatch(entry->name, entry->manufacturer,
                                             data.hasMfgData, data.manufacturerId,
                                             data.has128Svc, data.uuid128,
                                             data.svcCount, data.svc16);
            if (sig[0]) {
                strncpy(entry->sigLabel, sig, sizeof(entry->sigLabel) - 1);
                entry->sigLabel[sizeof(entry->sigLabel) - 1] = '\0';
            }
        }
        // Shield check
        if (s_shieldUp && !entry->isAlert && !isKnownDevice(data.mac)) {
            entry->isAlert = true;
            s_alertCount++;
        }
    }

    // -- Process Classic BT results --
    if (!s_classicInProgress && s_classicResultCount > 0 && s_scanState != ScanState::ClassicInquiry) {
        for (int i = 0; i < s_classicResultCount; i++) {
            ClassicBTDevice *cd = &s_classicResults[i];
            if (!passesRssiFilter(cd->rssi)) continue;

            BTDeviceEntry *entry = upsertDevice(cd->mac, BTSource::Classic);
            if (!entry) continue;

            entry->rssi = cd->rssi;
            if (cd->hasName && cd->name[0]) {
                strncpy(entry->name, cd->name, BT_NAME_LEN - 1);
            }
            entry->classOfDevice = cd->classOfDevice;

            if (g_captureJson && !entry->logged && !wasLogged(cd->mac)) {
                char line[176];
                snprintf(line, sizeof(line),
                    "{\"src\":\"classic\",\"mac\":\"%s\",\"rssi\":%d,\"name\":\"%s\",\"cod\":\"0x%06X\"}",
                    entry->macStr, (int)cd->rssi, entry->name, (unsigned)cd->classOfDevice);
                Serial.print("JSON ");
                Serial.println(line);
                sdLogAppend(line);
                entry->logged = true;
                markLogged(cd->mac);
            }

            entry->deviceType = decodeClassOfDevice(cd->classOfDevice);

            if (s_shieldUp && !entry->isAlert && !isKnownDevice(cd->mac)) {
                entry->isAlert = true;
                s_alertCount++;
            }
        }
        s_classicResultCount = 0;
        sortDevices();
    }
    // ── Classic BT inquiry scheduling ──
    if (s_classicEnabled &&
        s_scanState == ScanState::BLEScanning &&
        millis() - s_lastClassicInquiry > CLASSIC_INQUIRY_INTERVAL_MS) {
        bleStopScan();
        delay(100);
        classicStartInquiry();
    }

    // ── Classic inquiry finished → resume BLE ──
    if (s_scanState == ScanState::ClassicInquiry &&
        !s_classicInProgress && s_classicResultCount == 0 &&
        millis() - s_lastClassicInquiry > (CLASSIC_INQUIRY_SEC + 2) * 1000) {
        bleStartScan();
    }

    // If inquiry finished with results, the results are processed above
    // Then transition back to BLE
    if (s_scanState == ScanState::ClassicInquiry && !s_classicInProgress &&
        s_classicResultCount == 0) {
        bleStartScan();
    }

    // ── Expire devices not seen for 60+ seconds ──
    unsigned long now = millis();
    for (int i = s_deviceCount - 1; i >= 0; i--) {
        if (now - s_devices[i].lastSeenMs > 60000) {
            // Remove stale device
            if (s_devices[i].isAlert && s_alertCount > 0) s_alertCount--;
            if (i < s_deviceCount - 1)
                memmove(&s_devices[i], &s_devices[i + 1],
                        (s_deviceCount - i - 1) * sizeof(BTDeviceEntry));
            s_deviceCount--;
        }
    }

    // ── History ──
    updateHistory();

    // ── Sort ──
    sortDevices();
}

void btScannerGetAdvStats(uint32_t *reports, uint32_t *named) {
    if (reports) *reports = s_advSeen;
    if (named)   *named   = s_advNamed;
}

int btScannerGetDeviceCount() {
    return s_deviceCount;
}

const BTDeviceEntry* btScannerGetDevices() {
    return s_devices;
}

const BTDeviceEntry* btScannerGetDevice(int idx) {
    if (idx < 0 || idx >= s_deviceCount) return nullptr;
    return &s_devices[idx];
}

ScanState btScannerGetState() {
    return s_scanState;
}

void btScannerSetRssiFilter(int8_t minRssi) {
    s_rssiFilter = minRssi;
}

int8_t btScannerGetRssiFilter() {
    return s_rssiFilter;
}

void btScannerSetSortMode(SortMode mode) {
    s_sortMode = mode;
}

SortMode btScannerGetSortMode() {
    return s_sortMode;
}

void btScannerSetClassicEnabled(bool on) {
    s_classicEnabled = on;
    if (on && s_scanState == ScanState::BLEScanning) {
        // Will trigger next classic inquiry naturally via scheduler
    }
}

bool btScannerGetClassicEnabled() {
    return s_classicEnabled;
}

void btScannerSetShieldUp(bool up) {
    if (up == s_shieldUp) return;
    s_shieldUp = up;
    if (up) {
        s_alertCount = 0;
        // Clear alert flag on all current devices (they're now "known")
        for (int i = 0; i < s_deviceCount; i++) {
            s_devices[i].isAlert = false;
        }
        snapshotKnownDevices();
    } else {
        s_alertCount = 0;
        for (int i = 0; i < s_deviceCount; i++) {
            s_devices[i].isAlert = false;
        }
        s_knownCount = 0;
    }
}

bool btScannerGetShieldUp() {
    return s_shieldUp;
}

int btScannerGetAlertCount() {
    return s_alertCount;
}

void btScannerClearAlerts() {
    s_alertCount = 0;
    for (int i = 0; i < s_deviceCount; i++) {
        s_devices[i].isAlert = false;
    }
    if (s_shieldUp) {
        snapshotKnownDevices();
    }
}

void btScannerClearDevices() {
    s_deviceCount = 0;
    s_alertCount = 0;
    s_knownCount = 0;
    memset(s_devices, 0, sizeof(s_devices));
    memset(s_history, 0, sizeof(s_history));
    s_historyIdx = 0;
    if (s_shieldUp) {
        snapshotKnownDevices();
    }
}

bool btScannerDeleteDevice(int idx) {
    if (idx < 0 || idx >= s_deviceCount) return false;
    if (s_devices[idx].isAlert) {
        s_devices[idx].isAlert = false;
        if (s_alertCount > 0) s_alertCount--;
    }
    if (idx < s_deviceCount - 1)
        memmove(&s_devices[idx], &s_devices[idx + 1],
                (s_deviceCount - idx - 1) * sizeof(BTDeviceEntry));
    s_deviceCount--;
    return true;
}

int btScannerGetHistoryPoint(int idx) {
    // Map linear index to circular buffer: 0 = newest
    if (idx < 0 || idx >= HISTORY_POINTS) return -1;
    int actualIdx = (s_historyIdx - 1 - idx + HISTORY_POINTS) % HISTORY_POINTS;
    return s_history[actualIdx];
}

void btScannerGetHistoryRaw(int *buf, int maxLen) {
    // Fill buf with most recent points (buf[0] = oldest, buf[maxLen-1] = newest)
    if (maxLen > HISTORY_POINTS) maxLen = HISTORY_POINTS;
    for (int i = 0; i < maxLen; i++) {
        buf[i] = btScannerGetHistoryPoint(maxLen - 1 - i);
    }
}
