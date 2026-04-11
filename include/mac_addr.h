#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAC_ADDR_LEN 6
#define MAC_ADDR_COMPACT_STR_LEN 13
#define MAC_ADDR_COLON_STR_LEN 18

// Compare two MAC addresses for equality.
bool mac_addr_equal(const uint8_t a[MAC_ADDR_LEN], const uint8_t b[MAC_ADDR_LEN]);

// Return true if at least one byte in the MAC address is non-zero.
bool mac_addr_is_nonzero(const uint8_t mac[MAC_ADDR_LEN]);

// Format MAC as AABBCCDDEEFF. out_len must be >= MAC_ADDR_COMPACT_STR_LEN.
bool mac_addr_to_compact_str(const uint8_t mac[MAC_ADDR_LEN], char *out, size_t out_len);

// Format MAC as AA:BB:CC:DD:EE:FF. out_len must be >= MAC_ADDR_COLON_STR_LEN.
bool mac_addr_to_colon_str(const uint8_t mac[MAC_ADDR_LEN], char *out, size_t out_len);
