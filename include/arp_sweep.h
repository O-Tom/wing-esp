#pragma once

#include <stdbool.h>
#include "esp_netif.h"
#include "OLED.h"

// Initialize the ARP sweep module (clears cache)
void arp_sweep_init(void);

// Perform an ARP sweep on the DHCP pool and update the OLED
// verbose: if true, log detailed information about each device
void arp_sweep_perform(esp_netif_t *netif, OLEDDisplay *oled, bool verbose);

// Task function for periodic runtime scanning
// Pass a pointer to the netif and OLED display as context
typedef struct {
    esp_netif_t *netif;
    OLEDDisplay *oled;
} ARPSweepTaskContext;

void arp_sweep_runtime_scan_task(void *arg);
