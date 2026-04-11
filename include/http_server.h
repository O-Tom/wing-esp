#pragma once

#include "esp_err.h"

// Start the HTTP server with captive portal endpoints
// Registers handlers for:
// - /generate_204, /gen_204 (204 No Content)
// - /hotspot-detect.html (HTML success page)
// - /ncsi.txt (Microsoft NCSI)
// - / (WingESP info page)
esp_err_t http_server_start(void);
