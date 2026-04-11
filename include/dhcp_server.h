#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "esp_netif.h"
#include "OLED.h"

// Applies static IPv4 settings, configures DHCP server options, starts DHCPS,
// and returns the redirect IPv4 address in network byte order.
esp_err_t dhcp_server_apply_eth_config(esp_netif_t *netif, OLEDDisplay *oled, uint32_t *out_redirect_ip_be);
