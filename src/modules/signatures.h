// Detection signatures, generated from signatures.json by tools/gen-signatures.mjs.
// Do not hand-edit: change signatures.json and regenerate.
// 4 rules.
#pragma once
#include <stdint.h>

#define SIG_COUNT 4

typedef struct {
    const char    *label;        // short class shown in the UI
    const char    *name_prefix;  // "" = any
    const char    *vendor_not;   // "" = no vendor exclusion
    uint16_t       cid;          // 0 = any company id
    uint16_t       svc16;        // 0 = any 16-bit service
    const uint8_t *u128;         // nullptr = any 128-bit service
} SigRule;

static const uint8_t SIG_U128_2[16] = { 0x3E, 0x1D, 0x50, 0xCD, 0x7E, 0x3E, 0x42, 0x7D, 0x8E, 0x1C, 0xB7, 0x8A, 0xA8, 0x7F, 0xE6, 0x24 };

static const SigRule SIG_RULES[SIG_COUNT] = {
    { "Light", "Govee_", "", 0x8803, 0x0000, nullptr },
    { "Appliance", "AM410", "", 0x0000, 0x0000, nullptr },
    { "Sensor", "", "", 0x0000, 0x0000, SIG_U128_2 },
    { "FindMy", "", "Apple", 0x004C, 0x0000, nullptr },
};
