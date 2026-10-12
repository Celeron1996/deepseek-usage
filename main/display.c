#include "display.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

static const char *TAG = "display";

/* LCD pin mapping taken from the PCBA (same as the old pc_monitor project) */
#define LCD_GPIO_SCLK       15
#define LCD_GPIO_MOSI       9
#define LCD_GPIO_RST        16
#define LCD_GPIO_DC         13
#define LCD_GPIO_CS         11

#define LCD_HOST            SPI2_HOST
#define LCD_PIXEL_CLOCK_HZ  (40 * 1000 * 1000)

#define LCD_H_RES           240
#define LCD_V_RES           240
#define LCD_X_GAP           0           /* 1.54" 240x240 panel wired to the full frame */
#define LCD_Y_GAP           0           /* set to 80 if the panel window starts at row 80 */
#define LCD_CMD_BITS        8
#define LCD_PARAM_BITS      8

#define LCD_DRAW_BUF_LINES  40          /* LVGL draw buffer height, 240x40 px per buffer */

static esp_lcd_panel_handle_t s_panel = NULL;

esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "init SPI bus (MOSI=%d SCLK=%d CS=%d DC=%d RST=%d)",
             LCD_GPIO_MOSI, LCD_GPIO_SCLK, LCD_GPIO_CS, LCD_GPIO_DC, LCD_GPIO_RST);

    const spi_bus_config_t bus_cfg = {
        .mosi_io_num = LCD_GPIO_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .sclk_io_num = LCD_GPIO_SCLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_H_RES * LCD_DRAW_BUF_LINES * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO), TAG, "spi bus init failed");

    esp_lcd_panel_io_handle_t io = NULL;
    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_GPIO_CS,
        .dc_gpio_num = LCD_GPIO_DC,
        .spi_mode = 0,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io), TAG,
                        "panel io init failed");

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_GPIO_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io, &panel_cfg, &s_panel), TAG, "panel create failed");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG, "panel invert failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, LCD_X_GAP, LCD_Y_GAP), TAG, "panel gap failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "panel on failed");

    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "lvgl port init failed");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = s_panel,
        .buffer_size = LCD_H_RES * LCD_DRAW_BUF_LINES,
        .double_buffer = true,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
#if LVGL_VERSION_MAJOR >= 9
        .color_format = LV_COLOR_FORMAT_RGB565,
#endif
        .flags = {
            .buff_dma = true,
#if LVGL_VERSION_MAJOR >= 9
            .swap_bytes = true,
#endif
        },
    };
    ESP_RETURN_ON_FALSE(lvgl_port_add_disp(&disp_cfg) != NULL, ESP_FAIL, TAG, "add lvgl display failed");

    ESP_LOGI(TAG, "LCD ready (%dx%d @ %d MHz SPI)", LCD_H_RES, LCD_V_RES, LCD_PIXEL_CLOCK_HZ / 1000000);
    return ESP_OK;
}
