#include "dhcp_server.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "lwip/ip4_addr.h"
#include "lwip/inet.h"
#include "apps/dhcpserver/dhcpserver.h"

#include "arp_sweep.h"
#include "ip_v4.h"
#include "wing_esp.h"

#define DHCP_LEASE_TIME_UNITS ((DHCP_LEASE_TIME_SECONDS + (CONFIG_LWIP_DHCPS_LEASE_UNIT - 1)) / CONFIG_LWIP_DHCPS_LEASE_UNIT)

esp_err_t dhcp_server_apply_eth_config(esp_netif_t *netif, OLEDDisplay *oled, uint32_t *out_redirect_ip_be)
{
    esp_netif_ip_info_t ip = {0};
    esp_netif_str_to_ip4(ETH_IP_ADDR, &ip.ip);
    esp_netif_str_to_ip4(ETH_GATEWAY, &ip.gw);
    esp_netif_str_to_ip4(ETH_NETMASK, &ip.netmask);

    esp_err_t dhcps_stop = esp_netif_dhcps_stop(netif);
    if (dhcps_stop != ESP_OK && dhcps_stop != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGW(LOG_TAG, "DHCP server stop returned: %s", esp_err_to_name(dhcps_stop));
    }

    esp_err_t dhcpc_stop = esp_netif_dhcpc_stop(netif);
    if (dhcpc_stop != ESP_OK && dhcpc_stop != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGW(LOG_TAG, "DHCP client stop returned: %s", esp_err_to_name(dhcpc_stop));
    }

    esp_err_t set_ip_ret = esp_netif_set_ip_info(netif, &ip);
    if (set_ip_ret != ESP_OK) {
        ESP_LOGE(LOG_TAG, "Failed to set static IP info: %s", esp_err_to_name(set_ip_ret));
        return set_ip_ret;
    }

    dhcps_lease_t lease = {
        .enable = true,
    };
    if (!ip_v4_parse(DHCP_RANGE_START, &lease.start_ip) || !ip_v4_parse(DHCP_RANGE_END, &lease.end_ip)) {
        ESP_LOGE(LOG_TAG, "Invalid DHCP range format: %s - %s", DHCP_RANGE_START, DHCP_RANGE_END);
        return ESP_ERR_INVALID_ARG;
    }
    // On some esp-netif backends this option is ignored; we log explicitly if so.
    esp_err_t dhcp_lease_ret = esp_netif_dhcps_option(netif,
        ESP_NETIF_OP_SET,
        ESP_NETIF_REQUESTED_IP_ADDRESS,
        &lease,
        sizeof(lease));
    if (dhcp_lease_ret != ESP_OK) {
        ESP_LOGW(LOG_TAG, "DHCP lease pool option not applied: %s (DHCPS may use implementation default pool)", esp_err_to_name(dhcp_lease_ret));
    }

    uint32_t lease_time_units = DHCP_LEASE_TIME_UNITS;
    esp_err_t lease_time_ret = esp_netif_dhcps_option(netif,
        ESP_NETIF_OP_SET,
        ESP_NETIF_IP_ADDRESS_LEASE_TIME,
        &lease_time_units,
        sizeof(lease_time_units));
    if (lease_time_ret != ESP_OK) {
        ESP_LOGW(LOG_TAG,
            "DHCP lease time option not applied: %s (requested=%us units=%u unit_size=%us)",
            esp_err_to_name(lease_time_ret),
            (unsigned int)DHCP_LEASE_TIME_SECONDS,
            (unsigned int)lease_time_units,
            (unsigned int)CONFIG_LWIP_DHCPS_LEASE_UNIT);
    } else {
        ESP_LOGI(LOG_TAG,
            "DHCP lease time set to ~%us (units=%u unit_size=%us)",
            (unsigned int)DHCP_LEASE_TIME_SECONDS,
            (unsigned int)lease_time_units,
            (unsigned int)CONFIG_LWIP_DHCPS_LEASE_UNIT);
    }

    // This is the supported path to advertise DNS from DHCP on this stack.
    esp_netif_dns_info_t dns_info = {
        .ip.type = ESP_IPADDR_TYPE_V4,
        .ip.u_addr.ip4 = ip.ip,
    };
    esp_err_t dns_set_ret = esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info);
    if (dns_set_ret != ESP_OK) {
        ESP_LOGW(LOG_TAG, "Failed to set DHCP-advertised DNS via ESP_NETIF_DNS_MAIN: %s", esp_err_to_name(dns_set_ret));
    } else {
        ESP_LOGI(LOG_TAG, "DHCP DNS main server set to %s via esp_netif_set_dns_info", ETH_IP_ADDR);
    }

    arp_sweep_perform(netif, oled, true);

    esp_err_t dhcps_start = ESP_FAIL;
    for (int attempt = 1; attempt <= DHCPS_START_RETRY_COUNT; attempt++) {
        dhcps_start = esp_netif_dhcps_start(netif);
        if (dhcps_start == ESP_OK || dhcps_start == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
            break;
        }
        ESP_LOGW(LOG_TAG, "DHCP server start attempt %d/%d failed: %s",
            attempt,
            DHCPS_START_RETRY_COUNT,
            esp_err_to_name(dhcps_start));
        vTaskDelay(pdMS_TO_TICKS(DHCPS_START_RETRY_DELAY_MS));
    }
    if (dhcps_start != ESP_OK && dhcps_start != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
        ESP_LOGW(LOG_TAG, "DHCP server start returned: %s", esp_err_to_name(dhcps_start));
    } else if (dhcps_start == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
        ESP_LOGW(LOG_TAG, "DHCP server already started");
    } else {
        ESP_LOGI(LOG_TAG, "DHCP server started (router=%s dns=%s pool=%s-%s)",
            ETH_GATEWAY,
            ETH_IP_ADDR,
            DHCP_RANGE_START,
            DHCP_RANGE_END);
    }

    esp_netif_dhcp_status_t dhcps_status = ESP_NETIF_DHCP_INIT;
    if (esp_netif_dhcps_get_status(netif, &dhcps_status) == ESP_OK) {
        ESP_LOGI(LOG_TAG, "DHCP server status=%d (0:init 1:started 2:stopped)", dhcps_status);
    }

    if (out_redirect_ip_be != NULL) {
        *out_redirect_ip_be = ip4_addr_get_u32((const ip4_addr_t *)&ip.ip);
    }

    if (oled != NULL) {
        OLED_SetOwnIP(oled, ETH_IP_ADDR);
    }

    return ESP_OK;
}
