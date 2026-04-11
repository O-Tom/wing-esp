#pragma once

#include "esp_err.h"
#include "esp_eth.h"
#include "esp_netif.h"
// Logs the board-specific Ethernet/SPI wiring and speed config.
void ethernet_init_log_hw_config(const char *tag);

// Create and configure Ethernet netif + W5500 driver and attach them.
// Uses board defaults defined in ethernet_init.c.
// out_netif and out_eth_handle must be non-NULL.
esp_err_t ethernet_init(esp_netif_t **out_netif, esp_eth_handle_t *out_eth_handle);
