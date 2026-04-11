#include "ip_v4.h"

#include <stdio.h>

#include "lwip/inet.h"

bool ip_v4_parse(const char *ip_str, ip4_addr_t *out_ip)
{
    if (ip_str == NULL || out_ip == NULL) {
        return false;
    }

    return ip4addr_aton(ip_str, out_ip) != 0;
}

bool ip_v4_parse_octets(const char *ip_str, uint8_t out_octets[4])
{
    unsigned int o1 = 0;
    unsigned int o2 = 0;
    unsigned int o3 = 0;
    unsigned int o4 = 0;

    if (ip_str == NULL || out_octets == NULL) {
        return false;
    }

    if (sscanf(ip_str, "%u.%u.%u.%u", &o1, &o2, &o3, &o4) != 4) {
        return false;
    }

    if (o1 > 255 || o2 > 255 || o3 > 255 || o4 > 255) {
        return false;
    }

    out_octets[0] = (uint8_t)o1;
    out_octets[1] = (uint8_t)o2;
    out_octets[2] = (uint8_t)o3;
    out_octets[3] = (uint8_t)o4;
    return true;
}

void ip_v4_from_octets(ip4_addr_t *out_ip, uint8_t o1, uint8_t o2, uint8_t o3, uint8_t o4)
{
    if (out_ip == NULL) {
        return;
    }

    IP4_ADDR(out_ip, o1, o2, o3, o4);
}

bool ip_v4_to_str(const ip4_addr_t *ip, char *out, size_t out_len)
{
    if (ip == NULL || out == NULL || out_len == 0) {
        return false;
    }

    return ip4addr_ntoa_r(ip, out, (int)out_len) != NULL;
}

bool ip_v4_u32_to_str(uint32_t ip_u32, char *out, size_t out_len)
{
    ip4_addr_t ip = {
        .addr = ip_u32,
    };
    return ip_v4_to_str(&ip, out, out_len);
}
