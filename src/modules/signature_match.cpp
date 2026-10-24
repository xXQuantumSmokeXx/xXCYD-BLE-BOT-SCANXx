#include "signature_match.h"
#include "signatures.h"
#include <cstring>

const char* signatureMatch(const char *name, const char *vendor,
                           bool hasCid, uint16_t cid,
                           bool has128, const uint8_t *u128,
                           uint8_t svcCount, const uint16_t *svc16) {
    for (int i = 0; i < SIG_COUNT; i++) {
        const SigRule &r = SIG_RULES[i];

        if (r.cid && (!hasCid || cid != r.cid)) continue;

        if (r.name_prefix[0]) {
            const size_t n = strlen(r.name_prefix);
            if (!name || strncmp(name, r.name_prefix, n) != 0) continue;
        }

        if (r.vendor_not[0] && vendor && strcmp(vendor, r.vendor_not) == 0) continue;

        if (r.svc16) {
            bool found = false;
            for (uint8_t k = 0; k < svcCount; k++) {
                if (svc16[k] == r.svc16) { found = true; break; }
            }
            if (!found) continue;
        }

        if (r.u128) {
            if (!has128 || !u128 || memcmp(u128, r.u128, 16) != 0) continue;
        }

        return r.label;
    }
    return "";
}