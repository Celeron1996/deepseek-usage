#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Init the push button (GPIO1, active low, pulled up).
 */
esp_err_t button_init(void);

/**
 * @brief Block until the button is pressed or the timeout expires.
 *
 * @param timeout_ms  Timeout in milliseconds.
 * @return true if the button was pressed (debounced and released), false on timeout.
 */
bool button_wait_for_press(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
