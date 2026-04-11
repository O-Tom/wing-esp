#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "wing_esp.h"

typedef struct OLEDDisplay OLEDDisplay;

OLEDDisplay* OLED_Create(void);
void OLED_Destroy(OLEDDisplay* oled);
esp_err_t OLED_Init(OLEDDisplay* oled);
void OLED_UpdateState(OLEDDisplay* oled, const ARPScanResult *scan);
void OLED_Render(OLEDDisplay* oled);
bool OLED_IsReady(OLEDDisplay* oled);
void OLED_SetOwnIP(OLEDDisplay* oled, const char *ip);
void OLED_ClearArpDhcpOnLinkDown(OLEDDisplay* oled);
