#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lwip/ip4_addr.h"

// Parse dotted IPv4 string into lwIP ip4_addr_t.
bool ip_v4_parse(const char *ip_str, ip4_addr_t *out_ip);

// Parse dotted IPv4 string into 4 octets.
bool ip_v4_parse_octets(const char *ip_str, uint8_t out_octets[4]);

// Build lwIP ip4_addr_t from octets.
void ip_v4_from_octets(ip4_addr_t *out_ip, uint8_t o1, uint8_t o2, uint8_t o3, uint8_t o4);

// Format lwIP ip4_addr_t as dotted IPv4 string.
bool ip_v4_to_str(const ip4_addr_t *ip, char *out, size_t out_len);

// Format a raw lwIP IPv4 value (ip4_addr_t.addr-compatible) as dotted IPv4 string.
bool ip_v4_u32_to_str(uint32_t ip_u32, char *out, size_t out_len);
