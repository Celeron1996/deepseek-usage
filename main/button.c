#include "button.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "button";

#define BUTTON_GPIO          1
#define BUTTON_DEBOUNCE_MS   30
#define BUTTON_LONG_PRESS_MS 5000

static SemaphoreHandle_t s_sem;

static void IRAM_ATTR button_isr(void *arg)
{
    BaseType_t high_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(s_sem, &high_task_woken);
    if (high_task_woken) {
        portYIELD_FROM_ISR();
    }
}

esp_err_t button_init(void)
{
    s_sem = xSemaphoreCreateBinary();
    if (s_sem == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const gpio_config_t io_cfg = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_cfg));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BUTTON_GPIO, button_isr, NULL));

    ESP_LOGI(TAG, "button ready on GPIO%d (short press: refresh, 5s hold: factory reset)", BUTTON_GPIO);
    return ESP_OK;
}

int button_wait(uint32_t timeout_ms)
{
    xSemaphoreTake(s_sem, 0); /* flush stale edges */

    if (xSemaphoreTake(s_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return BUTTON_TIMEOUT;
    }

    /* debounce: the level must still be low, otherwise it was a glitch */
    vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));
    if (gpio_get_level(BUTTON_GPIO) != 0) {
        return BUTTON_TIMEOUT;
    }

    int held_ms = BUTTON_DEBOUNCE_MS;
    while (gpio_get_level(BUTTON_GPIO) == 0) {
        vTaskDelay(pdMS_TO_TICKS(50));
        held_ms += 50;
        if (held_ms >= BUTTON_LONG_PRESS_MS) {
            return BUTTON_LONG_PRESS;
        }
    }
    return BUTTON_SHORT_PRESS;
}
