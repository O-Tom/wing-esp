#include "ethernet_init.h"

#include "esp_eth.h"
#include "esp_eth_mac_spi.h"
#include "esp_eth_netif_glue.h"
#include "esp_log.h"
#include "esp_netif_defaults.h"
#include "esp_netif_net_stack.h"

#include "driver/spi_master.h"
#include "wing_esp.h"

// Waveshare ESP32-S3-ETH uses W5500 over SPI.
#ifndef ETH_SPI_HOST
#define ETH_SPI_HOST SPI2_HOST
#endif

#ifndef ETH_SPI_MISO_GPIO
#define ETH_SPI_MISO_GPIO 12
#endif

#ifndef ETH_SPI_MOSI_GPIO
#define ETH_SPI_MOSI_GPIO 11
#endif

#ifndef ETH_SPI_SCLK_GPIO
#define ETH_SPI_SCLK_GPIO 13
#endif

#ifndef ETH_SPI_CS_GPIO
#define ETH_SPI_CS_GPIO 14
#endif

#ifndef ETH_SPI_INT_GPIO
#define ETH_SPI_INT_GPIO 10
#endif

#ifndef ETH_PHY_ADDR
#define ETH_PHY_ADDR 0
#endif

#ifndef ETH_PHY_RST_GPIO
#define ETH_PHY_RST_GPIO 9
#endif

#ifndef ETH_SPI_CLOCK_MHZ
#define ETH_SPI_CLOCK_MHZ 20
#endif

void ethernet_init_log_hw_config(const char *tag)
{
    const char *log_tag = (tag != NULL) ? tag : LOG_TAG;
    ESP_LOGI(log_tag, "Board Ethernet: W5500 over SPI");
    ESP_LOGI(log_tag, "SPI host=%d miso=%d mosi=%d sclk=%d cs=%d int=%d rst=%d phy_addr=%d clk=%dMHz",
        ETH_SPI_HOST,
        ETH_SPI_MISO_GPIO,
        ETH_SPI_MOSI_GPIO,
        ETH_SPI_SCLK_GPIO,
        ETH_SPI_CS_GPIO,
        ETH_SPI_INT_GPIO,
        ETH_PHY_RST_GPIO,
        ETH_PHY_ADDR,
        ETH_SPI_CLOCK_MHZ);
}

esp_err_t ethernet_init(esp_netif_t **out_netif,
                        esp_eth_handle_t *out_eth_handle)
{
    if (out_netif == NULL || out_eth_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_netif_inherent_config_t eth_base_cfg = ESP_NETIF_INHERENT_DEFAULT_ETH();
    eth_base_cfg.flags = (esp_netif_flags_t)((eth_base_cfg.flags & ~ESP_NETIF_DHCP_CLIENT) |
                                             ESP_NETIF_DHCP_SERVER |
                                             ESP_NETIF_FLAG_AUTOUP);
    eth_base_cfg.if_key = "ETH_DHCPS";
    eth_base_cfg.if_desc = "eth_dhcps";

    esp_netif_config_t netif_cfg = {
        .base = &eth_base_cfg,
        .driver = NULL,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
    };

    esp_netif_t *netif = esp_netif_new(&netif_cfg);
    if (netif == NULL) {
        ESP_LOGE(LOG_TAG, "Failed to create Ethernet netif");
        return ESP_FAIL;
    }

    spi_bus_config_t buscfg = {
        .miso_io_num = ETH_SPI_MISO_GPIO,
        .mosi_io_num = ETH_SPI_MOSI_GPIO,
        .sclk_io_num = ETH_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    esp_err_t ret = spi_bus_initialize(ETH_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(LOG_TAG, "spi_bus_initialize failed: %s", esp_err_to_name(ret));
        return ret;
    }

    spi_device_interface_config_t spi_devcfg = {
        .command_bits = 16,
        .address_bits = 8,
        .mode = 0,
        .clock_speed_hz = ETH_SPI_CLOCK_MHZ * 1000 * 1000,
        .spics_io_num = ETH_SPI_CS_GPIO,
        .queue_size = 20,
    };

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = ETH_PHY_ADDR;
    phy_config.reset_gpio_num = ETH_PHY_RST_GPIO;

    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(ETH_SPI_HOST, &spi_devcfg);
    w5500_config.int_gpio_num = ETH_SPI_INT_GPIO;

    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    eth_config.mac = mac;
    eth_config.phy = phy;

    esp_eth_handle_t eth_handle = NULL;
    ret = esp_eth_driver_install(&eth_config, &eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(LOG_TAG, "esp_eth_driver_install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    uint8_t local_mac[6] = {0x02, 0x00, 0x00, 0x12, 0x34, 0x56};
    ret = esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, local_mac);
    if (ret != ESP_OK) {
        ESP_LOGE(LOG_TAG, "esp_eth_ioctl(ETH_CMD_S_MAC_ADDR) failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_eth_netif_glue_handle_t glue = esp_eth_new_netif_glue(eth_handle);
    ret = esp_netif_attach(netif, glue);
    if (ret != ESP_OK) {
        ESP_LOGE(LOG_TAG, "esp_netif_attach failed: %s", esp_err_to_name(ret));
        return ret;
    }

    *out_netif = netif;
    *out_eth_handle = eth_handle;
    return ESP_OK;
}
