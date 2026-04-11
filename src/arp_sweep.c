#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lwip/def.h"
#include "lwip/etharp.h"
#include "lwip/ip4_addr.h"
#include "lwip/inet.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_defaults.h"
#include "esp_netif_net_stack.h"
//
#include "arp_sweep.h"
#include "ip_v4.h"
#include "mac_addr.h"
#include "wing_esp.h"
#include "tracked_devices.h"

typedef struct {
    uint32_t ip;
    uint32_t last_seen_ticks;
    uint8_t mac[6];
    bool has_mac;
} arp_device_cache_entry_t;

// ARP device cache uses ARP_POOL_SIZE as the maximum
static arp_device_cache_entry_t s_device_cache[ARP_POOL_SIZE] = {0};
static size_t s_device_cache_count = 0;

// =====================================================================
// Helper functions
// =====================================================================

static void append_unique_ip(char list[][MAX_DEVICE_NAME_LEN], size_t *count, size_t max_count, const char *ip_str)
{
    for (size_t i = 0; i < *count; i++) {
        if (strcmp(list[i], ip_str) == 0) {
            return;
        }
    }

    if (*count < max_count) {
        strncpy(list[*count], ip_str, MAX_DEVICE_NAME_LEN - 1);
        list[*count][MAX_DEVICE_NAME_LEN - 1] = '\0';
        (*count)++;
    }
}

static void format_device_entry(char *out, size_t out_len, const uint8_t mac[6], unsigned int last_octet)
{
    char device_name[32] = {0};

    if (out == NULL || out_len == 0) {
        return;
    }

    build_device_name(device_name, sizeof(device_name), mac);

    snprintf(out, out_len, "%u %s", last_octet, device_name);
    out[out_len - 1] = '\0';
}

typedef struct {
    struct netif *netif;
    ip4_addr_t ip;
    err_t ret;
} arp_request_ctx_t;

typedef struct {
    struct netif *netif;
    ip4_addr_t ip;
    bool found;
    uint8_t mac[6];
} arp_find_ctx_t;

typedef struct {
    struct netif *netif;
} arp_cleanup_ctx_t;

static void arp_request_cb(void *arg)
{
    arp_request_ctx_t *ctx = (arp_request_ctx_t *)arg;
    ctx->ret = etharp_request(ctx->netif, &ctx->ip);
}

static void arp_find_cb(void *arg)
{
    arp_find_ctx_t *ctx = (arp_find_ctx_t *)arg;
    struct eth_addr *eth_ret = NULL;
    const ip4_addr_t *ip_ret = NULL;
    ssize_t idx = etharp_find_addr(ctx->netif, &ctx->ip, &eth_ret, &ip_ret);
    if (idx >= 0 && eth_ret != NULL) {
        ctx->found = true;
        ctx->mac[0] = eth_ret->addr[0];
        ctx->mac[1] = eth_ret->addr[1];
        ctx->mac[2] = eth_ret->addr[2];
        ctx->mac[3] = eth_ret->addr[3];
        ctx->mac[4] = eth_ret->addr[4];
        ctx->mac[5] = eth_ret->addr[5];
    }
}

static void arp_cleanup_cb(void *arg)
{
    arp_cleanup_ctx_t *ctx = (arp_cleanup_ctx_t *)arg;
    etharp_cleanup_netif(ctx->netif);
}

static arp_device_cache_entry_t *arp_cache_find(uint32_t ip)
{
    for (size_t i = 0; i < s_device_cache_count; i++) {
        if (s_device_cache[i].ip == ip) {
            return &s_device_cache[i];
        }
    }

    return NULL;
}

static void arp_cache_update(uint32_t ip, const uint8_t mac[6])
{
    uint32_t now = xTaskGetTickCount();
    bool is_new = true;

    for (size_t i = 0; i < s_device_cache_count; i++) {
        if (s_device_cache[i].ip == ip) {
            s_device_cache[i].last_seen_ticks = now;
            if (mac_addr_is_nonzero(mac)) {
                memcpy(s_device_cache[i].mac, mac, 6);
                s_device_cache[i].has_mac = true;
            }
            is_new = false;
            return;
        }
    }

    if (s_device_cache_count < ARP_POOL_SIZE) {
        s_device_cache[s_device_cache_count].ip = ip;
        s_device_cache[s_device_cache_count].last_seen_ticks = now;
        s_device_cache[s_device_cache_count].has_mac = false;
        if (mac_addr_is_nonzero(mac)) {
            memcpy(s_device_cache[s_device_cache_count].mac, mac, 6);
            s_device_cache[s_device_cache_count].has_mac = true;
        }
        s_device_cache_count++;

        if (is_new) {
            char ip_str[16] = {0};
            char device_name[MAX_DEVICE_NAME_LEN] = {0};
            if (!ip_v4_u32_to_str(ip, ip_str, sizeof(ip_str))) {
                snprintf(ip_str, sizeof(ip_str), "invalid");
            }
            if (mac_addr_is_nonzero(mac)) {
                build_device_name(device_name, sizeof(device_name), mac);
                ESP_LOGI(LOG_TAG, "[FOUND] %s - %s", ip_str, device_name);
            } else {
                ESP_LOGI(LOG_TAG, "[FOUND] %s - new device detected (no MAC yet)", ip_str);
            }
        }
    }
}

static bool arp_cache_is_within_guard(uint32_t ip)
{
    uint32_t now = xTaskGetTickCount();
    uint32_t guard_ticks = pdMS_TO_TICKS(ARP_DEVICE_GUARD_TIME_MS);

    for (size_t i = 0; i < s_device_cache_count; i++) {
        if (s_device_cache[i].ip == ip) {
            uint32_t age = now - s_device_cache[i].last_seen_ticks;
            return (age < guard_ticks);
        }
    }
    return false;
}

static void arp_cache_cleanup_expired(void)
{
    uint32_t now = xTaskGetTickCount();
    uint32_t guard_ticks = pdMS_TO_TICKS(ARP_DEVICE_GUARD_TIME_MS);
    size_t write_idx = 0;

    for (size_t i = 0; i < s_device_cache_count; i++) {
        uint32_t age = now - s_device_cache[i].last_seen_ticks;
        if (age < guard_ticks) {
            s_device_cache[write_idx] = s_device_cache[i];
            write_idx++;
        } else {
            // Device guard time expired
            uint32_t ip_be = s_device_cache[i].ip;
            char ip_str[16] = {0};
            char device_name[MAX_DEVICE_NAME_LEN] = {0};
            if (!ip_v4_u32_to_str(ip_be, ip_str, sizeof(ip_str))) {
                snprintf(ip_str, sizeof(ip_str), "invalid");
            }
            if (s_device_cache[i].has_mac) {
                build_device_name(device_name, sizeof(device_name), s_device_cache[i].mac);
                ESP_LOGI(LOG_TAG, "[REMOVED] %s - %s (guard expired)", ip_str, device_name);
            } else {
                ESP_LOGI(LOG_TAG, "[REMOVED] %s - guard time expired", ip_str);
            }
        }
    }
    s_device_cache_count = write_idx;
}

// =====================================================================
// Public API
// =====================================================================

void arp_sweep_init(void)
{
    s_device_cache_count = 0;
    memset(s_device_cache, 0, sizeof(s_device_cache));
}

void arp_sweep_perform(esp_netif_t *netif, OLEDDisplay *oled, bool verbose)
{
    uint8_t start_octets[4] = {0};
    uint8_t end_octets[4] = {0};

    if (!ip_v4_parse_octets(DHCP_RANGE_START, start_octets) ||
        !ip_v4_parse_octets(DHCP_RANGE_END, end_octets)) {
        ESP_LOGW(LOG_TAG, "ARP sweep skipped: invalid DHCP range format");
        return;
    }

    if (start_octets[0] != end_octets[0] ||
        start_octets[1] != end_octets[1] ||
        start_octets[2] != end_octets[2] ||
        start_octets[3] > end_octets[3]) {
        ESP_LOGW(LOG_TAG, "ARP sweep supports only a single /24 contiguous range; skipping");
        return;
    }

    // Get lwip netif from esp_netif handle via private API
    struct netif *lwip_netif = (struct netif *)esp_netif_get_netif_impl(netif);
    if (lwip_netif == NULL) {
        ESP_LOGW(LOG_TAG, "ARP sweep skipped: netif backend unavailable");
        return;
    }

    ARPScanResult *scan = (ARPScanResult *)calloc(1, sizeof(ARPScanResult));
    if (scan == NULL) {
        ESP_LOGW(LOG_TAG, "ARP sweep: out of memory");
        return;
    }

    if (verbose) {
        ESP_LOGI(LOG_TAG, "ARP sweep on DHCP pool %s-%s", DHCP_RANGE_START, DHCP_RANGE_END);
    }

#if ARP_FLUSH_CACHE_BEFORE_SWEEP
    arp_cleanup_ctx_t cleanup = {
        .netif = lwip_netif,
    };
    err_t cleanup_ret = tcpip_callback_wait(arp_cleanup_cb, &cleanup);
    if (cleanup_ret != ERR_OK) {
        ESP_LOGW(LOG_TAG, "ARP cache flush failed before sweep (cb=%d)", (int)cleanup_ret);
    } else if (verbose) {
        ESP_LOGI(LOG_TAG, "ARP cache flushed before sweep (fresh-reply mode)");
    }
#endif

    // Send ARP probes
    for (unsigned int octet = start_octets[3]; octet <= end_octets[3]; octet++) {
        char ip_str[16] = {0};
        ip4_addr_t target = {0};
        ip_v4_from_octets(&target,
            start_octets[0],
            start_octets[1],
            start_octets[2],
            (uint8_t)octet);

        if (!ip_v4_to_str(&target, ip_str, sizeof(ip_str))) {
            snprintf(ip_str, sizeof(ip_str), "invalid");
        }

        arp_request_ctx_t req = {
            .netif = lwip_netif,
            .ip = target,
            .ret = ERR_ARG,
        };
        err_t cb_ret = tcpip_callback_wait(arp_request_cb, &req);
        if (cb_ret != ERR_OK || req.ret != ERR_OK) {
            if (verbose) {
                ESP_LOGW(LOG_TAG, "ARP request failed for %s (cb=%d req=%d)", ip_str, (int)cb_ret, (int)req.ret);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(ARP_PRE_DHCP_REQUEST_SPACING_MS));
    }

    vTaskDelay(pdMS_TO_TICKS(ARP_PRE_DHCP_SETTLE_MS));

    // Collect responses
    int found = 0;
    size_t guard_count = 0;
    for (unsigned int octet = start_octets[3]; octet <= end_octets[3]; octet++) {
        char ip_str[16] = {0};
        char device_entry[MAX_DEVICE_NAME_LEN] = {0};
        ip4_addr_t target = {0};
        ip_v4_from_octets(&target,
            start_octets[0],
            start_octets[1],
            start_octets[2],
            (uint8_t)octet);

        if (!ip_v4_to_str(&target, ip_str, sizeof(ip_str))) {
            snprintf(ip_str, sizeof(ip_str), "invalid");
        }

        uint32_t ip_uint = target.addr;

        arp_find_ctx_t find = {
            .netif = lwip_netif,
            .ip = target,
            .found = false,
            .mac = {0},
        };
        err_t cb_ret = tcpip_callback_wait(arp_find_cb, &find);
        if (cb_ret != ERR_OK) {
            continue;
        }

        bool include_in_result = false;

        if (find.found) {
            // Device responded to ARP probe: update cache and include in result
            arp_cache_update(ip_uint, find.mac);
            include_in_result = true;
            found++;

            const uint8_t *mac = find.mac;
            const char *known_name = tracked_device_name_from_mac(mac);
            char mac_str[MAC_ADDR_COLON_STR_LEN] = {0};
            const char *log_mac = mac_addr_to_colon_str(mac, mac_str, sizeof(mac_str)) ? mac_str : "invalid";
            if (verbose) {
                ESP_LOGI(LOG_TAG,
                    "[ACTIVE] %s mac=%s%s%s%s",
                    ip_str,
                    log_mac,
                    known_name ? " (" : "",
                    known_name ? known_name : "",
                    known_name ? ")" : "");
            }
        } else if (arp_cache_is_within_guard(ip_uint)) {
            // Device didn't respond, but within guard time window: include in result
            include_in_result = true;
            guard_count++;
            if (verbose) {
                ESP_LOGI(LOG_TAG, "[GUARD ] %s - no ARP response, removing in <30s", ip_str);
            }
        }

        if (include_in_result) {
            // Find MAC from cache if this is a guard-time entry
            uint8_t cached_mac[6] = {0};
            if (!find.found) {
                arp_device_cache_entry_t *entry = arp_cache_find(ip_uint);
                if (entry != NULL && entry->has_mac) {
                    memcpy(cached_mac, entry->mac, 6);
                }

                // Try to get MAC from current ARP table for display purposes
                arp_find_ctx_t cached_find = {
                    .netif = lwip_netif,
                    .ip = target,
                    .found = false,
                    .mac = {0},
                };
                tcpip_callback_wait(arp_find_cb, &cached_find);
                if (cached_find.found && mac_addr_is_nonzero(cached_find.mac)) {
                    memcpy(cached_mac, cached_find.mac, 6);
                    arp_cache_update(ip_uint, cached_find.mac);
                }
            } else {
                memcpy(cached_mac, find.mac, 6);
            }

            format_device_entry(device_entry, MAX_DEVICE_NAME_LEN, cached_mac, octet);
            append_unique_ip(scan->arp,
                &scan->arp_count,
                ARP_POOL_SIZE,
                device_entry);
        }
    }

    // Clean up expired entries from cache
    arp_cache_cleanup_expired();

    if (found == 0) {
        if (verbose) {
            ESP_LOGI(LOG_TAG, "ARP sweep: no active hosts detected in DHCP pool");
        }
    }

    if (!verbose) {
        ESP_LOGI(LOG_TAG,
            "[SWEEP] active=%d guard=%u shown=%u",
            found,
            (unsigned int)guard_count,
            (unsigned int)scan->arp_count);
    }

    OLED_UpdateState(oled, scan);
    free(scan);
}

void arp_sweep_runtime_scan_task(void *arg)
{
    ARPSweepTaskContext *ctx = (ARPSweepTaskContext *)arg;

    if (ctx == NULL) {
        ESP_LOGE(LOG_TAG, "ARP scan task: invalid context");
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        if (ctx->netif != NULL) {
            arp_sweep_perform(ctx->netif, ctx->oled, false);
        }
        vTaskDelay(pdMS_TO_TICKS(ARP_RUNTIME_SCAN_INTERVAL_MS));
    }
}
