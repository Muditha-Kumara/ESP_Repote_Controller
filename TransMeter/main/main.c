#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "types.h"
#include "joystick.h"
#include "esp_now_tx.h"
#include "wifi_config.h"
#include "web_server.h"

static const char *TAG = "TransMeter";

// Configuration constants
#define AP_SSID "Binaru"
#define AP_PASSWORD "binaru123"
#define MDNS_HOSTNAME "transmeter"
#define WIFI_CHANNEL 1

// Receiver MAC address (modify as needed)
// This should be the MAC address of your receiver device
static const uint8_t receiver_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // Broadcast

// Control parameters
#define SEND_INTERVAL_MS 50  // Send every 50ms (20 Hz)
#define WEB_SERVER_PORT 80

// Statistics
static uint32_t packet_count = 0;

/**
 * Main remote control task
 * Reads joystick axes and sends ESP-NOW packets
 */
static void remote_control_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Remote control task started");
    motor_control_t motor_data = {0};
    link_metrics_t metrics = {0};
    TickType_t last_wake_time = xTaskGetTickCount();

    while (1) {
        memset(&motor_data, 0, sizeof(motor_data));

        if (joystick_is_connected())
        {
            if (joystick_read(&motor_data) != 0)
            {
                ESP_LOGW(TAG, "Failed to read joystick state");
            }
        }
        motor_data.timestamp = esp_log_timestamp();

        web_server_update_motor_data(&motor_data);

        esp_now_tx_get_link_metrics(&metrics);
        web_server_update_link_metrics(metrics.receiver_rssi_dbm,
                                       metrics.estimated_distance_m,
                                       metrics.round_trip_time_us,
                                       metrics.valid);

        if (esp_now_tx_send(&motor_data) == 0)
        {
            packet_count++;

            if (packet_count % 20 == 0)
            {
                ESP_LOGI(TAG,
                         "Sent packet #%lu | M1:%d M2:%d",
                         packet_count,
                         motor_data.motor1_speed,
                         motor_data.motor2_speed);
            }
        }
        else
        {
            ESP_LOGW(TAG, "Failed to send packet");
        }

        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(SEND_INTERVAL_MS));
    }
}

/**
 * Application initialization
 */
void app_main(void)
{
    ESP_LOGI(TAG, "========== Remote Controller Transmitter Initialized ==========");
    ESP_LOGI(TAG, "FW Version: 1.0.0");
    ESP_LOGI(TAG, "ESP-NOW Long Range Mode: ENABLED");
    ESP_LOGI(TAG, "FTM distance measurement: enabled when the receiver responds");
    ESP_LOGI(TAG, "WiFi Mode: SoftAP only");
    ESP_LOGI(TAG, "Control Mode: WebSocket drive pad over AP");
    ESP_LOGI(TAG, "Build Date: %s %s", __DATE__, __TIME__);

    // Initialize NVS (Non-Volatile Storage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI(TAG, "Erasing NVS flash");
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Initialize WiFi and mDNS
    ESP_LOGI(TAG, "Initializing SoftAP (%s)...", AP_SSID);
    if (wifi_init(AP_SSID, AP_PASSWORD) != 0)
    {
        ESP_LOGE(TAG, "Failed to initialize WiFi");
        return;
    }
    ESP_LOGI(TAG, "✓ WiFi initialization started");

    // Initialize mDNS early so it is ready when the AP comes up.
    if (wifi_mdns_init(MDNS_HOSTNAME, WEB_SERVER_PORT) != 0)
    {
        ESP_LOGW(TAG, "mDNS startup failed, continuing without hostname discovery");
    }

    if (wifi_is_ap_active())
    {
        ESP_LOGI(TAG, "✓ SoftAP active: %s", wifi_get_ap_ip());
    }
    else
    {
        ESP_LOGW(TAG, "⚠ SoftAP is not active yet");
    }

    // Initialize ESP-NOW
    ESP_LOGI(TAG, "Initializing ESP-NOW with long range mode...");
    if (esp_now_tx_init(1, WIFI_CHANNEL) != 0) {  // 1 = long range enabled
        ESP_LOGE(TAG, "Failed to initialize ESP-NOW");
        return;
    }
    ESP_LOGI(TAG, "✓ ESP-NOW initialized");

    // Add receiver peer
    if (esp_now_tx_add_peer(receiver_mac, WIFI_CHANNEL) != 0) {
        ESP_LOGW(TAG, "Failed to add receiver peer (broadcast will still work)");
    }
    ESP_LOGI(TAG, "✓ Receiver peer configured");

    // Initialize USB joystick support.
    if (joystick_init() != 0)
    {
        ESP_LOGE(TAG, "Failed to initialize USB joystick support");
        return;
    }
    ESP_LOGI(TAG, "✓ USB joystick support initialized");

    // Initialize Web Server
    ESP_LOGI(TAG, "Initializing Web Server on port %d...", WEB_SERVER_PORT);
    if (web_server_init(WEB_SERVER_PORT) == 0) {
        ESP_LOGI(TAG, "✓ Web Server started");
        web_server_update_drive_command(0, 0, 0);
        ESP_LOGI(TAG, "  Access at: http://%s or http://transmeter.local",
                 wifi_get_ap_ip());
        ESP_LOGI(TAG, "  Direct AP access: connect to SSID '%s' and open http://%s",
                 AP_SSID, wifi_get_ap_ip());
    } else {
        ESP_LOGW(TAG, "⚠ Failed to initialize web server");
    }

    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "System initialization complete!");
    ESP_LOGI(TAG, "Ready to transmit motor control commands");
    ESP_LOGI(TAG, "==========================================================");

    // Create tasks on CPU 1 so CPU 0 can service WiFi/LwIP and idle watchdog.
    xTaskCreatePinnedToCore(remote_control_task, "RemoteCtrl", 4096, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, "All tasks created successfully");
}
