#include "mac_addr.h"

#include <stdio.h>
#include <string.h>

bool mac_addr_equal(const uint8_t a[MAC_ADDR_LEN], const uint8_t b[MAC_ADDR_LEN])
{
    if (a == NULL || b == NULL) {
        return false;
    }

    return memcmp(a, b, MAC_ADDR_LEN) == 0;
}

bool mac_addr_is_nonzero(const uint8_t mac[MAC_ADDR_LEN])
{
    if (mac == NULL) {
        return false;
    }

    for (size_t i = 0; i < MAC_ADDR_LEN; i++) {
        if (mac[i] != 0) {
            return true;
        }
    }

    return false;
}

bool mac_addr_to_compact_str(const uint8_t mac[MAC_ADDR_LEN], char *out, size_t out_len)
{
    if (mac == NULL || out == NULL || out_len < MAC_ADDR_COMPACT_STR_LEN) {
        return false;
    }

    int written = snprintf(out,
        out_len,
        "%02X%02X%02X%02X%02X%02X",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return written > 0 && (size_t)written < out_len;
}

bool mac_addr_to_colon_str(const uint8_t mac[MAC_ADDR_LEN], char *out, size_t out_len)
{
    if (mac == NULL || out == NULL || out_len < MAC_ADDR_COLON_STR_LEN) {
        return false;
    }

    int written = snprintf(out,
        out_len,
        "%02X:%02X:%02X:%02X:%02X:%02X",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return written > 0 && (size_t)written < out_len;
}
