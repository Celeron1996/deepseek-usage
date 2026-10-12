#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUTTON_TIMEOUT     0
#define BUTTON_SHORT_PRESS 1
#define BUTTON_LONG_PRESS  2

esp_err_t button_init(void);

/* Wait for a press (GPIO1, active low) and measure its duration.
 * Returns BUTTON_SHORT_PRESS, BUTTON_LONG_PRESS (>= 5 s) or BUTTON_TIMEOUT. */
int button_wait(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
