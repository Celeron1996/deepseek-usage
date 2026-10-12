#include "button.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "button";

#define BUTTON_GPIO         1
#define BUTTON_DEBOUNCE_MS  30
#define BUTTON_RELEASE_TIMEOUT_MS 2000

static SemaphoreHandle_t s_button_sem;

static void IRAM_ATTR button_isr_handler(void *arg)
{
    BaseType_t high_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(s_button_sem, &high_task_woken);
    if (high_task_woken) {
        portYIELD_FROM_ISR();
    }
}

esp_err_t button_init(void)
{
    s_button_sem = xSemaphoreCreateBinary();
    if (s_button_sem == NULL) {
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
    ESP_ERROR_CHECK(gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, NULL));

    ESP_LOGI(TAG, "button ready on GPIO%d (active low)", BUTTON_GPIO);
    return ESP_OK;
}

bool button_wait_for_press(uint32_t timeout_ms)
{
    if (xSemaphoreTake(s_button_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return false;
    }

    /* debounce: the level must still be low, then wait until it is released */
    vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));
    if (gpio_get_level(BUTTON_GPIO) != 0) {
        return false;
    }

    uint32_t waited = 0;
    while (gpio_get_level(BUTTON_GPIO) == 0 && waited < BUTTON_RELEASE_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(10));
        waited += 10;
    }

    return true;
}
