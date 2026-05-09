#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_now_rx.h"
#include "tb6612fng.h"

static const char *TAG = "Receiver";

#define COMMAND_TIMEOUT_MS 500

static void command_watchdog_task(void *pvParameters)
{
    (void)pvParameters;

    while (1) {
        uint32_t age_ms = (uint32_t)(esp_log_timestamp() - esp_now_rx_get_last_seen_ms());
        if (age_ms > COMMAND_TIMEOUT_MS) {
            tb6612fng_stop();
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Receiver starting");
    ESP_LOGI(TAG, "Thrust motor pins: PWM GPIO21, IN1 GPIO18, IN2 GPIO19");
    ESP_LOGI(TAG, "Steering motor pins: PWM GPIO22, IN1 GPIO4, IN2 GPIO5");
    ESP_LOGI(TAG, "TB6612 STBY pin: GPIO23");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(ret);
    }

    tb6612fng_init();
    ESP_ERROR_CHECK(esp_now_rx_init());

    tb6612fng_stop();

    xTaskCreatePinnedToCore(command_watchdog_task, "cmd_watchdog", 3072, NULL, 4, NULL, 1);

    ESP_LOGI(TAG, "Receiver ready: waiting for transmitter packets");
}
