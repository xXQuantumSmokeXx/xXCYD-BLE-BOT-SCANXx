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

#define BLE_QUEUE_SIZE     16     // entries in the BLE → main loop queue
#define MAX_CLASSIC_RESULTS 32    // per inquiry cycle

// ═══════════════════════════════════════════════════════════════════════════════
//  OUI Manufacturer Lookup Table
// ═══════════════════════════════════════════════════════════════════════════════

struct OUIEntry { const char* prefix; const char* name; };

static const OUIEntry OUI_TABLE[] = {
    // ── Apple ──
    {"D0:03:4B", "Apple"}, {"AC:DE:48", "Apple"}, {"00:25:00", "Apple"},
    {"3C:E0:72", "Apple"}, {"F0:18:98", "Apple"}, {"B8:53:AC", "Apple"},
    {"4C:32:75", "Apple"}, {"68:AB:1E", "Apple"}, {"B0:34:95", "Apple"},
    {"F0:D1:A9", "Apple"}, {"F4:0F:24", "Apple"}, {"A4:D1:8C", "Apple"},
    {"7C:6D:62", "Apple"}, {"88:66:A5", "Apple"}, {"08:66:98", "Apple"},
    // ── Samsung ──
    {"00:1B:44", "Samsung"}, {"00:15:99", "Samsung"}, {"94:35:0A", "Samsung"},
    {"F0:72:8C", "Samsung"}, {"CC:05:77", "Samsung"}, {"84:38:35", "Samsung"},
    {"B4:4B:D2", "Samsung"}, {"E4:12:1D", "Samsung"}, {"8C:F5:A3", "Samsung"},
    {"5C:41:E7", "Samsung"}, {"44:D1:FA", "Samsung"}, {"C8:D7:19", "Samsung"},
    {"B0:52:16", "Samsung"},
    // ── Google / Nest / Fitbit ──
    {"28:11:A5", "Google"}, {"00:1A:11", "Google"}, {"D8:3A:DD", "Google"},
    {"F4:F5:D8", "Google"}, {"3C:5A:B4", "Google"}, {"E4:F0:42", "Google"},
    {"54:60:09", "Google"}, {"70:3A:CB", "Google"}, {"8C:F7:10", "Google"},
    {"00:16:0A", "Fitbit"}, {"C0:28:8D", "Fitbit"},
    // ── Microsoft ──
    {"00:50:F2", "Microsoft"}, {"00:15:5D", "Microsoft"}, {"28:18:78", "Microsoft"},
    {"7C:1E:52", "Microsoft"}, {"B0:52:7C", "Microsoft"},
    // ── Xiaomi / Redmi / Poco ──
    {"00:1A:7D", "Xiaomi"}, {"F8:A7:63", "Xiaomi"}, {"28:6C:07", "Xiaomi"},
    {"D4:63:C6", "Xiaomi"}, {"C4:6E:1F", "Xiaomi"}, {"70:8B:CD", "Xiaomi"},
    {"64:09:80", "Xiaomi"}, {"80:28:ED", "Xiaomi"}, {"F0:F6:98", "Xiaomi"},
    {"28:E3:1F", "Xiaomi"}, {"04:77:85", "Xiaomi"},
    // ── Huawei / Honor ──
    {"00:1E:C1", "Huawei"}, {"30:FB:B8", "Huawei"}, {"DC:D9:16", "Huawei"},
    {"40:BF:17", "Huawei"}, {"B0:68:E6", "Huawei"}, {"F4:5C:89", "Huawei"},
    {"7C:B1:5D", "Huawei"}, {"A4:50:55", "Huawei"},
    // ── Sony ──
    {"00:01:4A", "Sony"}, {"00:24:BE", "Sony"}, {"30:F7:72", "Sony"},
    {"5C:96:56", "Sony"}, {"BC:30:D9", "Sony"}, {"AC:9B:0A", "Sony"},
    {"78:8B:2A", "Sony"}, {"F0:BF:97", "Sony"},
    // ── LG ──
    {"00:1A:8D", "LG"}, {"00:1D:C0", "LG"}, {"08:EE:8E", "LG"},
    {"B4:82:C5", "LG"}, {"F8:95:C7", "LG"}, {"04:4E:AF", "LG"},
    // ── Motorola / Lenovo ──
    {"00:0A:28", "Motorola"}, {"00:11:B9", "Motorola"}, {"F4:56:2F", "Motorola"},
    {"00:1B:EB", "OnePlus"}, {"C0:EE:FB", "OnePlus"},
    {"1C:3A:60", "Lenovo"}, {"00:0F:B5", "Lenovo"}, {"54:EE:75", "Lenovo"},
    // ── Oppo / Vivo / Realme ──
    {"00:1B:97", "Oppo"}, {"C8:28:32", "Vivo"}, {"14:A7:8B", "Oppo"},
    {"70:2E:97", "Oppo"}, {"E8:D4:83", "Vivo"},
    // ── Intel ──
    {"00:02:B3", "Intel"}, {"00:80:98", "Intel"}, {"E4:42:A6", "Intel"},
    {"00:21:5C", "Intel"}, {"A4:17:31", "Intel"},
    // ── Dell ──
    {"00:14:22", "Dell"}, {"00:1C:23", "Dell"}, {"F0:4D:A2", "Dell"},
    {"B8:CA:3A", "Dell"}, {"84:A9:3E", "Dell"},
    // ── HP ──
    {"00:1F:29", "HP"}, {"00:23:8B", "HP"}, {"64:51:06", "HP"},
    {"D8:9D:67", "HP"}, {"C8:CB:B8", "HP"},
    // ── Acer / ASUS ──
    {"00:26:2D", "Acer"}, {"4C:0B:BE", "Acer"}, {"70:8B:78", "Acer"},
    {"00:22:15", "ASUS"}, {"D8:50:E6", "ASUS"}, {"38:D5:47", "ASUS"},
    // ── Bose ──
    {"00:0C:8A", "Bose"}, {"5C:31:3E", "Bose"}, {"C8:84:47", "Bose"},
    {"04:52:C7", "Bose"}, {"78:2B:46", "Bose"},
    // ── JBL / Harman ──
    {"00:1B:FE", "JBL"}, {"C4:13:E2", "JBL"}, {"00:26:BB", "JBL"},
    {"00:0E:9F", "Harman"}, {"00:15:9E", "Harman"},
    // ── Sennheiser / Jabra / Plantronics ──
    {"00:1B:66", "Sennheiser"}, {"00:16:B8", "Jabra"}, {"00:0F:6D", "Jabra"},
    {"00:0F:56", "Plantronics"},
    // ── Skullcandy / Anker / Soundcore ──
    {"FC:E8:6F", "Skullcandy"}, {"00:1C:91", "Anker"}, {"E8:78:65", "Anker"},
    // ── Garmin / Polar / Amazfit ──
    {"00:1B:C3", "Garmin"}, {"04:02:1F", "Garmin"},
    {"00:22:D7", "Polar"},
    {"E0:0D:B5", "Amazfit"},
    // ── Nintendo / PlayStation / Xbox ──
    {"00:09:BF", "Nintendo"}, {"40:F4:07", "Nintendo"}, {"7C:BB:8A", "Nintendo"},
    {"00:1F:A7", "Nintendo"}, {"2C:10:C1", "Nintendo"},
    {"00:04:1F", "Sony PS"}, {"F8:46:1C", "Sony PS"},
    {"00:0D:3A", "MS Xbox"},
    // ── Raspberry Pi ──
    {"B8:27:EB", "Raspberry Pi"}, {"DC:A6:32", "Raspberry Pi"},
    {"E4:5F:01", "Raspberry Pi"},
    // ── ESP32 / Espressif ──
    {"24:0A:C4", "ESP32"}, {"30:AE:A4", "ESP32"}, {"A0:20:A6", "ESP32"},
    {"78:E3:6D", "ESP32"}, {"08:D1:F9", "ESP32"}, {"B0:A7:32", "ESP32"},
    {"A8:03:2A", "ESP32"},
    // ── Nordic / nRF ──
    {"00:1A:CF", "Nordic"}, {"DC:25:EF", "Nordic"}, {"F1:92:D1", "Nordic"},
    {"D7:28:5F", "Nordic"}, {"E4:31:45", "Nordic"},
    // ── Tile ──
    {"04:52:F3", "Tile"},
    // ── Philips / TP-Link / Belkin ──
    {"00:0D:E7", "Philips"}, {"00:17:88", "Philips"},
    {"00:1D:0F", "TP-Link"}, {"00:1D:6A", "TP-Link"},
    {"00:0D:72", "Belkin"},
    // ── Logitech ──
    {"00:07:61", "Logitech"}, {"00:1F:20", "Logitech"}, {"88:C6:26", "Logitech"},
    // ── HTC / Nokia / BlackBerry ──
    {"00:00:CA", "HTC"}, {"00:23:76", "HTC"},
    {"00:1E:3A", "Nokia"}, {"00:0E:ED", "Nokia"},
    // ── Tesla / Ford / Toyota (car BLE) ──
    {"00:17:A1", "Toyota"}, {"00:26:55", "Ford"},
    // ── DJI / GoPro ──
    {"60:60:1F", "DJI"}, {"34:D2:62", "DJI"},
    {"E0:23:4D", "GoPro"},
    // ── Withings / Peloton / misc wearables ──
    {"00:24:E4", "Withings"},
    // ── Generic chipsets ──
    {"00:02:72", "TI"}, {"00:02:6B", "TI"},
    {"00:0B:57", "SiliconLabs"},
    {"00:02:5B", "CSR"},
    {"00:0A:9C", "Broadcom"},
    {"00:10:60", "Broadcom"},
};

static const int OUI_COUNT = sizeof(OUI_TABLE) / sizeof(OUI_TABLE[0]);

static const char* lookupOUI(const char* macStr) {
    // Extract first 8 chars "XX:XX:XX" and match
    char prefix[9];
    memcpy(prefix, macStr, 8);
    prefix[8] = '\0';
    // Uppercase
    for (int i = 0; i < 8; i++) {
        if (prefix[i] >= 'a' && prefix[i] <= 'f') prefix[i] -= 32;
    }
    for (int i = 0; i < OUI_COUNT; i++) {
        if (strcmp(prefix, OUI_TABLE[i].prefix) == 0) return OUI_TABLE[i].name;
    }
    return "Unknown";
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
};

static QueueHandle_t s_bleQueue = nullptr;

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

        // RSSI
        data.rssi = advertisedDevice.getRSSI();

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

        // Push to queue — non-blocking, drop if full
        xQueueSend(s_bleQueue, &data, 0);
    }
};

static BLEScan *s_pBLEScan = nullptr;

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
    BLEDevice::init("CYD-BLE-BOT-SCAN");

    s_pBLEScan = BLEDevice::getScan();
    s_pBLEScan->setAdvertisedDeviceCallbacks(new BleScanCallbacks());
    s_pBLEScan->setActiveScan(true);
    s_pBLEScan->setInterval(BLE_SCAN_INTERVAL_MS);
    s_pBLEScan->setWindow(BLE_SCAN_WINDOW_MS);

    s_bleQueue = xQueueCreate(BLE_QUEUE_SIZE, sizeof(BLEDeviceData));

    Serial.println("BT: BLE stack OK");

    // ── Attempt Classic BT (BR/EDR) — may not work on all ESP32 BLE stacks ──
    // The controller is in BLE-only mode from BLEDevice::init().
    // We try a single-shot upgrade to BTDM — if it fails, we accept BLE-only.
    esp_err_t ret = esp_bt_controller_enable(ESP_BT_MODE_BTDM);
    if (ret == ESP_OK) {
        esp_bt_gap_register_callback(classicBTCallback);
        esp_bt_dev_set_device_name("CYD-BLE-BOT-SCAN");
        Serial.println("BT: Classic BT enabled (dual-mode OK)");
        s_classicEnabled = true;
    } else {
        // Can't enable Classic — that's fine, BLE will still work
        Serial.printf("BT: Classic BT not available (err %d), BLE-only mode\n", ret);
        s_classicEnabled = false;
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

void btScannerUpdate() {
    // ── Drain BLE queue ──
    BLEDeviceData data;
    while (xQueueReceive(s_bleQueue, &data, 0) == pdTRUE) {
        if (!passesRssiFilter(data.rssi)) continue;

        BTDeviceEntry *entry = upsertDevice(data.mac, BTSource::BLE);
        if (!entry) continue;

        entry->rssi = data.rssi;

        if (data.hasName && data.name[0]) {
            strncpy(entry->name, data.name, BT_NAME_LEN - 1);
        }
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
        }

        // Shield check
        if (s_shieldUp && !entry->isAlert && !isKnownDevice(data.mac)) {
            entry->isAlert = true;
            s_alertCount++;
        }
    }

    // ── Process Classic BT results ──
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
