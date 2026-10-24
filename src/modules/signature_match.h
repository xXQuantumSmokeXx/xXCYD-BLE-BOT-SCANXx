#pragma once
#include <cstdint>

// Returns the label of the first matching detection rule, or "" when none match.
// Only the fields a rule constrains are checked; see signatures.h.
const char* signatureMatch(const char *name, const char *vendor,
                           bool hasCid, uint16_t cid,
                           bool has128, const uint8_t *u128,
                           uint8_t svcCount, const uint16_t *svc16);