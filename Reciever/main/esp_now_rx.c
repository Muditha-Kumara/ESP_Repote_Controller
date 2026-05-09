#include "esp_now_rx.h"

#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "tb6612fng.h"

static const char *TAG = "ESPNOW_RX";

static motor_control_t latest_command = {0};
static uint32_t last_seen_ms = 0;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    (void)recv_info;

    if (data == NULL || len != (int)sizeof(motor_control_t)) {
        ESP_LOGW(TAG, "Ignoring packet with invalid size: %d", len);
        return;
    }

    memcpy(&latest_command, data, sizeof(latest_command));
    last_seen_ms = now_ms();

    /* Log received packet details to serial for debugging/telemetry */
    if (recv_info && recv_info->src_addr) {
        ESP_LOGI(TAG, "Received %d-byte packet from %02x:%02x:%02x:%02x:%02x:%02x",
                 len,
                 recv_info->src_addr[0], recv_info->src_addr[1], recv_info->src_addr[2],
                 recv_info->src_addr[3], recv_info->src_addr[4], recv_info->src_addr[5]);
    } else {
        ESP_LOGI(TAG, "Received %d-byte packet (src unknown)", len);
    }

    /* Print parsed motor control values */
    ESP_LOGI(TAG, "motor1: speed=%d dir=%d; motor2: speed=%d dir=%d; ts=%u",
             latest_command.motor1_speed, latest_command.motor1_direction,
             latest_command.motor2_speed, latest_command.motor2_direction,
             latest_command.timestamp);

    /* Also dump raw payload bytes */
    ESP_LOG_BUFFER_HEXDUMP(TAG, data, len, ESP_LOG_DEBUG);

    tb6612fng_apply(&latest_command);
}

esp_err_t esp_now_rx_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_LR));

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(recv_cb));

    last_seen_ms = now_ms();
    ESP_LOGI(TAG, "ESP-NOW receiver ready on channel 1 with LR protocol enabled");
    return ESP_OK;
}

bool esp_now_rx_get_latest(motor_control_t *command)
{
    if (command == NULL) {
        return false;
    }

    memcpy(command, &latest_command, sizeof(*command));
    return true;
}

uint32_t esp_now_rx_get_last_seen_ms(void)
{
    return last_seen_ms;
}
