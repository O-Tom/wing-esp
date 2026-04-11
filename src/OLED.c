#include "OLED.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "u8g2.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define OLED_I2C_PORT        0
#define OLED_I2C_ADDR        0x3C
#define OLED_SDA_GPIO        18
#define OLED_SCL_GPIO        16
#define OLED_WIDTH           128
#define OLED_HEIGHT          64
#define OLED_ROWS_PER_PAGE   4
#define OLED_PAGE_FLIP_MS    5000
#define OLED_LIST_TEXT_Y0    20
#define OLED_LIST_LINE_PITCH 9

/* Single-display project: the I2C handle is shared via a static so that
 * the U8g2 byte callback can reach it without user_ptr (disabled by default
 * for non-Linux embedded targets in this version of U8g2). */
static i2c_master_dev_handle_t s_i2c_dev = NULL;

struct OLEDDisplay {
    i2c_master_bus_handle_t i2c_bus;
    i2c_master_dev_handle_t i2c_dev;
    u8g2_t                  u8g2;
    bool                    ready;
    char                    own_ip[16];
    char                    arp_ips[ARP_POOL_SIZE][MAX_DEVICE_NAME_LEN];
    size_t                  arp_count;
    SemaphoreHandle_t       state_lock;
};

/* ── U8g2 platform callbacks ──────────────────────────────────────────── */

/*
 * Byte callback: accumulates bytes between START/END_TRANSFER and sends them
 * in a single i2c_master_transmit call.  The ESP-IDF I2C device handle is
 * stored in u8x8->user_ptr so the callback is stateless w.r.t. the display
 * struct.  Max payload per page-data transfer is 129 bytes (1 control + 128
 * data), so a 132-byte static buffer is sufficient.
 */
static uint8_t u8x8_byte_hw_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    static uint8_t buf[132];
    static uint8_t buf_len;
    uint8_t *data;
    (void)u8x8;

    switch (msg) {
        case U8X8_MSG_BYTE_START_TRANSFER:
            buf_len = 0;
            break;

        case U8X8_MSG_BYTE_SEND:
            data = (uint8_t *)arg_ptr;
            while (arg_int-- > 0 && buf_len < (uint8_t)sizeof(buf)) {
                buf[buf_len++] = *data++;
            }
            break;

        case U8X8_MSG_BYTE_END_TRANSFER:
            if (s_i2c_dev != NULL) {
                i2c_master_transmit(s_i2c_dev, buf, buf_len, 50);
            }
            break;

        case U8X8_MSG_BYTE_INIT:
        case U8X8_MSG_BYTE_SET_DC:
            break;

        default:
            return 0;
    }
    return 1;
}

static uint8_t u8x8_gpio_and_delay(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    (void)u8x8;
    (void)arg_ptr;

    switch (msg) {
        case U8X8_MSG_GPIO_AND_DELAY_INIT:
            break;
        case U8X8_MSG_DELAY_MILLI:
            vTaskDelay(pdMS_TO_TICKS(arg_int ? arg_int : 1));
            break;
        case U8X8_MSG_DELAY_10MICRO:
            esp_rom_delay_us((uint32_t)arg_int * 10U);
            break;
        case U8X8_MSG_DELAY_100NANO:
            esp_rom_delay_us(1);
            break;
        case U8X8_MSG_GPIO_RESET:
            /* No hardware reset pin on this I2C OLED */
            break;
        default:
            return 0;
    }
    return 1;
}

/* ── Drawing helpers ──────────────────────────────────────────────────── */
/*
 * All helpers receive a u8g2_t* and assume:
 *   - u8g2_font_5x7_tf is already selected
 *   - u8g2_SetFontPosTop has been called so that DrawStr y == top-of-glyph
 *     (identical to the old draw_text_at coordinate convention)
 */

static void draw_ip_banner(u8g2_t *u8g2, const char *ip)
{
    int i;
    const char *shown = (ip && ip[0]) ? ip : "-.-.-.-";
    int w = (int)u8g2_GetStrWidth(u8g2, shown);
    int x = (OLED_WIDTH - w) / 2;
    if (x < 0) x = 0;

    u8g2_DrawStr(u8g2, (u8g2_uint_t)x, 0, shown);

    /* Small corner decorations matching original design */
    for (i = 0; i < 10; i++) {
        u8g2_DrawPixel(u8g2, (u8g2_uint_t)i, 0);
        u8g2_DrawPixel(u8g2, (u8g2_uint_t)(OLED_WIDTH - 1 - i), 0);
    }
}

static void draw_labeled_box(u8g2_t *u8g2, int x0, int y0, int x1, int y1, const char *label)
{
    /* Full rectangular frame */
    u8g2_DrawFrame(u8g2,
                   (u8g2_uint_t)x0, (u8g2_uint_t)y0,
                   (u8g2_uint_t)(x1 - x0 + 1), (u8g2_uint_t)(y1 - y0 + 1));

    /* Simulate 1-pixel rounded corners: clear corner pixels, draw inset dots */
    u8g2_SetDrawColor(u8g2, 0);
    u8g2_DrawPixel(u8g2, (u8g2_uint_t)x0,       (u8g2_uint_t)y0);
    u8g2_DrawPixel(u8g2, (u8g2_uint_t)x1,       (u8g2_uint_t)y0);
    u8g2_DrawPixel(u8g2, (u8g2_uint_t)x0,       (u8g2_uint_t)y1);
    u8g2_DrawPixel(u8g2, (u8g2_uint_t)x1,       (u8g2_uint_t)y1);
    u8g2_SetDrawColor(u8g2, 1);
    u8g2_DrawPixel(u8g2, (u8g2_uint_t)(x0 + 1), (u8g2_uint_t)(y0 + 1));
    u8g2_DrawPixel(u8g2, (u8g2_uint_t)(x1 - 1), (u8g2_uint_t)(y0 + 1));
    u8g2_DrawPixel(u8g2, (u8g2_uint_t)(x0 + 1), (u8g2_uint_t)(y1 - 1));
    u8g2_DrawPixel(u8g2, (u8g2_uint_t)(x1 - 1), (u8g2_uint_t)(y1 - 1));

    if (label && label[0]) {
        int lw = (int)u8g2_GetStrWidth(u8g2, label);
        int lx = (x0 + x1 - lw) / 2;

        /* Erase gap in top border (lx-2 .. lx+lw+1) */
        u8g2_SetDrawColor(u8g2, 0);
        u8g2_DrawHLine(u8g2, (u8g2_uint_t)(lx - 2), (u8g2_uint_t)y0, (u8g2_uint_t)(lw + 4));
        u8g2_SetDrawColor(u8g2, 1);

        /* Label: top of glyph at y0-3, straddling the top border */
        u8g2_DrawStr(u8g2, (u8g2_uint_t)lx, (u8g2_uint_t)(y0 - 3), label);
    }
}

/* ── Public API ───────────────────────────────────────────────────────── */

OLEDDisplay *OLED_Create(void)
{
    OLEDDisplay *oled = (OLEDDisplay *)calloc(1, sizeof(OLEDDisplay));
    if (oled == NULL) {
        return NULL;
    }

    oled->state_lock = xSemaphoreCreateMutex();
    if (oled->state_lock == NULL) {
        free(oled);
        return NULL;
    }

    return oled;
}

void OLED_Destroy(OLEDDisplay *oled)
{
    if (oled == NULL) {
        return;
    }

    if (oled->i2c_dev != NULL && oled->i2c_bus != NULL) {
        i2c_master_bus_rm_device(oled->i2c_dev);
        oled->i2c_dev = NULL;
    }
    if (oled->i2c_bus != NULL) {
        i2c_del_master_bus(oled->i2c_bus);
        oled->i2c_bus = NULL;
    }
    if (oled->state_lock != NULL) {
        vSemaphoreDelete(oled->state_lock);
        oled->state_lock = NULL;
    }

    free(oled);
}

esp_err_t OLED_Init(OLEDDisplay *oled)
{
    i2c_master_bus_config_t bus_config;
    i2c_device_config_t dev_cfg;
    esp_err_t ret;

    if (oled == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&bus_config, 0, sizeof(bus_config));
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.i2c_port = OLED_I2C_PORT;
    bus_config.sda_io_num = OLED_SDA_GPIO;
    bus_config.scl_io_num = OLED_SCL_GPIO;
    bus_config.flags.enable_internal_pullup = true;

    ret = i2c_new_master_bus(&bus_config, &oled->i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGW(LOG_TAG, "OLED: i2c_new_master_bus failed: %s", esp_err_to_name(ret));
        return ret;
    }

    memset(&dev_cfg, 0, sizeof(dev_cfg));
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = OLED_I2C_ADDR;
    dev_cfg.scl_speed_hz = 400000;

    ret = i2c_master_bus_add_device(oled->i2c_bus, &dev_cfg, &oled->i2c_dev);
    if (ret != ESP_OK) {
        ESP_LOGW(LOG_TAG, "OLED: i2c_master_bus_add_device failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_master_probe(oled->i2c_bus, OLED_I2C_ADDR, 200);
    if (ret != ESP_OK) {
        ESP_LOGW(LOG_TAG, "OLED: probe failed for 0x%02X: %s", OLED_I2C_ADDR, esp_err_to_name(ret));
        return ret;
    }

    /* Set up U8g2: SH1106 128x64, full frame buffer, 180-degree rotation.
     * U8g2 owns the SH1106 init sequence, column-offset handling, and
     * page-mode flushing -- all of which were previously hand-coded here. */
    s_i2c_dev = oled->i2c_dev;
    u8g2_Setup_sh1106_i2c_128x64_noname_f(
        &oled->u8g2, U8G2_R2,
        u8x8_byte_hw_i2c, u8x8_gpio_and_delay);
    u8g2_SetI2CAddress(&oled->u8g2, (uint8_t)(OLED_I2C_ADDR << 1));

    u8g2_InitDisplay(&oled->u8g2);
    u8g2_SetPowerSave(&oled->u8g2, 0);
    u8g2_ClearDisplay(&oled->u8g2);

    oled->ready = true;
    ESP_LOGI(LOG_TAG, "OLED ready (U8g2/SH1106) addr=0x%02X scl=%d sda=%d",
             OLED_I2C_ADDR, OLED_SCL_GPIO, OLED_SDA_GPIO);
    return ESP_OK;
}

void OLED_UpdateState(OLEDDisplay *oled, const ARPScanResult *scan)
{
    size_t i;

    if (oled == NULL || scan == NULL || oled->state_lock == NULL) {
        return;
    }

    if (xSemaphoreTake(oled->state_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        oled->arp_count = scan->arp_count;
        for (i = 0; i < ARP_POOL_SIZE; i++) {
            if (i < scan->arp_count) {
                strncpy(oled->arp_ips[i], scan->arp[i], sizeof(oled->arp_ips[i]) - 1);
                oled->arp_ips[i][sizeof(oled->arp_ips[i]) - 1] = '\0';
            } else {
                oled->arp_ips[i][0] = '\0';
            }
        }

        xSemaphoreGive(oled->state_lock);
    }
}

void OLED_Render(OLEDDisplay *oled)
{
    char ip[16] = {0};
    char arp[ARP_POOL_SIZE][MAX_DEVICE_NAME_LEN] = {{0}};
    size_t arp_count = 0;
    char line[28] = {0};
    char page_label[8] = {0};
    size_t total_pages;
    size_t current_page;
    size_t page_start;
    size_t page_end;
    size_t i;
    u8g2_t *u8g2;

    if (oled == NULL || !oled->ready || oled->state_lock == NULL) {
        return;
    }

    if (xSemaphoreTake(oled->state_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        strncpy(ip, oled->own_ip, sizeof(ip) - 1);
        ip[sizeof(ip) - 1] = '\0';

        arp_count = oled->arp_count;
        if (arp_count > ARP_POOL_SIZE) {
            arp_count = ARP_POOL_SIZE;
        }
        for (i = 0; i < arp_count; i++) {
            strncpy(arp[i], oled->arp_ips[i], sizeof(arp[i]) - 1);
            arp[i][sizeof(arp[i]) - 1] = '\0';
        }

        xSemaphoreGive(oled->state_lock);
    }

    /* Paging */
    if (arp_count == 0) {
        total_pages = 1;
        current_page = 0;
    } else {
        total_pages = (arp_count + OLED_ROWS_PER_PAGE - 1) / OLED_ROWS_PER_PAGE;
        current_page = (size_t)((xTaskGetTickCount() * (uint32_t)portTICK_PERIOD_MS)
                                / OLED_PAGE_FLIP_MS) % total_pages;
    }
    page_start = current_page * OLED_ROWS_PER_PAGE;
    page_end   = page_start + OLED_ROWS_PER_PAGE;
    if (page_end > arp_count) {
        page_end = arp_count;
    }

    u8g2 = &oled->u8g2;
    u8g2_ClearBuffer(u8g2);
    /* u8g2_SetFontPosTop makes DrawStr y == top-of-glyph, matching the old
     * draw_text_at(oled, x, y, ...) coordinate convention exactly. */
    u8g2_SetFont(u8g2, u8g2_font_mozart_nbp_tr);
    u8g2_SetFontPosTop(u8g2);
    u8g2_SetDrawColor(u8g2, 1);

    draw_ip_banner(u8g2, ip);
    draw_labeled_box(u8g2, 0, 12, OLED_WIDTH - 1, OLED_HEIGHT - 1, "Connected devices");

    if (arp_count == 0) {
        u8g2_DrawStr(u8g2, 4, OLED_LIST_TEXT_Y0, "<None>");
    } else {
        for (i = page_start; i < page_end; i++) {
            snprintf(line, sizeof(line), "%s", arp[i]);
            u8g2_DrawStr(u8g2, 4,
                         (u8g2_uint_t)(OLED_LIST_TEXT_Y0 + (int)(i - page_start) * OLED_LIST_LINE_PITCH),
                         line);
        }
    }

    /* Page indicator at bottom-right, straddling the box bottom border */
    if (total_pages > 1) {
        int lw, lx;
        u8g2_uint_t clear_x;

        snprintf(page_label, sizeof(page_label), "%u/%u",
                 (unsigned int)(current_page + 1), (unsigned int)total_pages);
        lw = (int)u8g2_GetStrWidth(u8g2, page_label);
        lx = (OLED_WIDTH - 1) - 3 - lw;
        if (lx < 0) {
            lx = 0;
        }

        /* Clear behind label to avoid OR-overlap with border pixels */
        clear_x = (lx >= 1) ? (u8g2_uint_t)(lx - 1) : 0;
        u8g2_SetDrawColor(u8g2, 0);
        u8g2_DrawBox(u8g2, clear_x, (u8g2_uint_t)(OLED_HEIGHT - 8), (u8g2_uint_t)(lw + 3), 8);
        u8g2_SetDrawColor(u8g2, 1);
        u8g2_DrawStr(u8g2, (u8g2_uint_t)lx, OLED_HEIGHT - 8, page_label);
    }

    u8g2_SendBuffer(u8g2);
}

bool OLED_IsReady(OLEDDisplay *oled)
{
    if (oled == NULL) {
        return false;
    }
    return oled->ready;
}

void OLED_SetOwnIP(OLEDDisplay *oled, const char *ip)
{
    if (oled == NULL || ip == NULL || oled->state_lock == NULL) {
        return;
    }

    if (xSemaphoreTake(oled->state_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        strncpy(oled->own_ip, ip, sizeof(oled->own_ip) - 1);
        oled->own_ip[sizeof(oled->own_ip) - 1] = '\0';
        xSemaphoreGive(oled->state_lock);
    }
}

void OLED_ClearArpDhcpOnLinkDown(OLEDDisplay *oled)
{
    if (oled == NULL || oled->state_lock == NULL) {
        return;
    }

    if (xSemaphoreTake(oled->state_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        oled->arp_count = 0;
        xSemaphoreGive(oled->state_lock);
    }
}
