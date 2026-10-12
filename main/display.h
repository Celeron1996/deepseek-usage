#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Init SPI bus, ST7789 panel and the LVGL port.
 */
esp_err_t display_init(void);

#ifdef __cplusplus
}
#endif
