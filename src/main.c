#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_eth.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "driver/gpio.h"

#include "OLED.h"
#include "mac_addr.h"
#include "tracked_devices.h"
#include "wing_esp.h"
#include "arp_sweep.h"
#include "dns_server.h"
#include "dhcp_server.h"
#include "ethernet_init.h"
#include "http_server.h"

static esp_netif_t *s_eth_netif = NULL;
static uint32_t s_redirect_ip = 0;
static bool s_eth_l3_configured = false;

static OLEDDisplay *g_oled_display = NULL;

static void oled_render_task(void *arg)
{
	(void)arg;

	while (true) {
		OLED_Render(g_oled_display);
		vTaskDelay(pdMS_TO_TICKS(1200));
	}
}

static void log_startup_config(void)
{
	ESP_LOGI(LOG_TAG, "=== WingESP Startup Config ===");
	ethernet_init_log_hw_config(LOG_TAG);
	ESP_LOGI(LOG_TAG, "LAN IPv4: ip=%s mask=%s gw=%s", ETH_IP_ADDR, ETH_NETMASK, ETH_GATEWAY);
	ESP_LOGI(LOG_TAG, "Upstream fallback gateway (not DHCP-advertised): %s", ETH_UPSTREAM_FALLBACK_GATEWAY);
	ESP_LOGI(LOG_TAG, "DHCP range: %s - %s", DHCP_RANGE_START, DHCP_RANGE_END);
	ESP_LOGI(LOG_TAG, "DHCP advertisement: router=%s (from netif gateway), dns=%s (set as ESP_NETIF_DNS_MAIN)", ETH_GATEWAY, ETH_IP_ADDR);
	for (size_t i = 0; i < tracked_device_count(); i++) {
		const tracked_device_t *dev = tracked_device_get(i);
		if (dev == NULL) {
			continue;
		}
		char mac_str[MAC_ADDR_COLON_STR_LEN] = {0};
		const char *log_mac = mac_addr_to_colon_str(dev->mac, mac_str, sizeof(mac_str)) ? mac_str : "invalid";
		ESP_LOGI(LOG_TAG,
			"Known device target: %s (%s)",
			dev->name,
			log_mac);
	}
	ESP_LOGI(LOG_TAG, "DNS: udp/%d mode=%s redirect_ip=%s",
		DNS_PORT,
		DNS_CAPTURE_ALL ? "wildcard" : "allow-list",
		ETH_IP_ADDR);
	ESP_LOGI(LOG_TAG, "HTTP endpoints: /, /generate_204, /gen_204, /hotspot-detect.html, /ncsi.txt");
	ESP_LOGI(LOG_TAG, "==============================");
}

static void on_eth_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
	(void)arg;
	(void)event_base;
	(void)event_data;

	if (event_id == ETHERNET_EVENT_CONNECTED) {
		ESP_LOGI(LOG_TAG, "Ethernet link up");
		if (!s_eth_l3_configured && s_eth_netif != NULL) {
			esp_err_t dhcp_ret = dhcp_server_apply_eth_config(s_eth_netif, g_oled_display, &s_redirect_ip);
			if (dhcp_ret == ESP_OK) {
				s_eth_l3_configured = true;
			}
		}
	} else if (event_id == ETHERNET_EVENT_DISCONNECTED) {
		ESP_LOGW(LOG_TAG, "Ethernet link down");
		s_eth_l3_configured = false;
		OLED_SetOwnIP(g_oled_display, "");
		OLED_ClearArpDhcpOnLinkDown(g_oled_display);
	}
}

void app_main(void)
{
	esp_err_t nvs_ret = nvs_flash_init();
	if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		nvs_ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(nvs_ret);
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &on_eth_event, NULL));

	esp_err_t gpio_isr_ret = gpio_install_isr_service(0);
	if (gpio_isr_ret != ESP_OK && gpio_isr_ret != ESP_ERR_INVALID_STATE) {
		ESP_LOGW(LOG_TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(gpio_isr_ret));
	}

#if defined(CONFIG_ETH_SPI_ETHERNET_W5500) && CONFIG_ETH_SPI_ETHERNET_W5500
	esp_eth_handle_t eth_handle = NULL;
	ESP_ERROR_CHECK(ethernet_init(&s_eth_netif, &eth_handle));

	log_startup_config();

	ESP_ERROR_CHECK(esp_eth_start(eth_handle));

	if (http_server_start() != ESP_OK) {
		ESP_LOGW(LOG_TAG, "HTTP server failed to start");
	}

	DNSServerTaskContext *dns_ctx = (DNSServerTaskContext *)malloc(sizeof(DNSServerTaskContext));
	if (dns_ctx != NULL) {
		dns_ctx->redirect_ip_be = &s_redirect_ip;
		xTaskCreatePinnedToCore(dns_server_task, "dns_server", 4096, dns_ctx, 5, NULL, tskNO_AFFINITY);
	}

	g_oled_display = OLED_Create();
	if (g_oled_display && OLED_Init(g_oled_display) == ESP_OK) {
		xTaskCreatePinnedToCore(oled_render_task, "oled_render", 4096, NULL, 3, NULL, tskNO_AFFINITY);
	} else {
		ESP_LOGW(LOG_TAG, "OLED not started (screen optional)");
	}

	arp_sweep_init();
	ARPSweepTaskContext *arp_ctx = (ARPSweepTaskContext *)malloc(sizeof(ARPSweepTaskContext));
	if (arp_ctx != NULL) {
		arp_ctx->netif = s_eth_netif;
		arp_ctx->oled = g_oled_display;
		xTaskCreatePinnedToCore(arp_sweep_runtime_scan_task, "arp_scan", 4096, arp_ctx, 4, NULL, tskNO_AFFINITY);
	}

	ESP_LOGI(LOG_TAG, "WingESP started: ETH %s, DNS and HTTP interception enabled", ETH_IP_ADDR);
#else
	ESP_LOGE(LOG_TAG, "W5500 support not enabled. Set CONFIG_ETH_SPI_ETHERNET_W5500=y in sdkconfig for this board.");
#endif
}