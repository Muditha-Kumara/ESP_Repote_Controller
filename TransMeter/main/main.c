#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "types.h"
#include "esp_now_tx.h"
#include "wifi_config.h"
#include "joystick.h"
#include "web_server.h"

static const char *TAG = "TransMeter";

// Configuration constants
#define WIFI_SSID "SLEngineers"
#define WIFI_PASSWORD "slengnet1"
#define MDNS_HOSTNAME "transmeter"
#define WIFI_CHANNEL 1

// ADC channels (use new API channel definitions)
#define JOYSTICK_ADC_CH_X 0        // ADC_CHANNEL_0 - GPIO36
#define JOYSTICK_ADC_CH_Y 3        // ADC_CHANNEL_3 - GPIO39
#define JOYSTICK_ADC_CH_X2 6       // ADC_CHANNEL_6 - GPIO34
#define JOYSTICK_ADC_CH_Y2 7       // ADC_CHANNEL_7 - GPIO35

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
 * Reads joystick and sends ESP-NOW packets
 */
static void remote_control_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Remote control task started");
    motor_control_t motor_data = {0};
    TickType_t last_wake_time = xTaskGetTickCount();
    remote_settings_t settings = {
        .long_range_enabled = 1,
        .power_level = 20,
        .send_interval_ms = SEND_INTERVAL_MS,
    };
    strncpy(settings.wifi_ssid, WIFI_SSID, sizeof(settings.wifi_ssid) - 1);

    web_server_update_settings(&settings);

    while (1) {
        // Read joystick and push one update per send interval.
        if (joystick_read(&motor_data) == 0) {
            // Update web server with joystick data
            uint16_t x, y, x2, y2;
            joystick_get_raw_values(&x, &y, &x2, &y2);
            web_server_update_adc_values(x, y, x2, y2);
            web_server_update_motor_data(&motor_data);

            // Send ESP-NOW packet
            if (esp_now_tx_send(&motor_data) == 0) {
                packet_count++;

                if (packet_count % 20 == 0) {  // Log every 20 packets
                    ESP_LOGI(TAG,
                             "Sent packet #%lu | M1:[%d,%d] M2:[%d,%d]",
                             packet_count,
                             motor_data.motor1_speed,
                             motor_data.motor1_direction,
                             motor_data.motor2_speed,
                             motor_data.motor2_direction);
                }
            } else {
                ESP_LOGW(TAG, "Failed to send packet");
            }
        }

        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(SEND_INTERVAL_MS));
    }
}

/**
 * Monitor WiFi connection
 */
static void wifi_monitor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "WiFi monitor task started");

    while (1) {
        if (wifi_is_connected()) {
            ESP_LOGI(TAG, "WiFi connected, IP: %s", wifi_get_local_ip());
        }
        vTaskDelay(pdMS_TO_TICKS(30000));  // Check every 30 seconds
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
    ESP_LOGI(TAG, "Build Date: %s %s", __DATE__, __TIME__);

    // Initialize NVS (Non-Volatile Storage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI(TAG, "Erasing NVS flash");
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Initialize Joystick ADC
    ESP_LOGI(TAG, "Initializing Joystick ADC...");
    if (joystick_init(JOYSTICK_ADC_CH_X, JOYSTICK_ADC_CH_Y,
                      JOYSTICK_ADC_CH_X2, JOYSTICK_ADC_CH_Y2) != 0) {
        ESP_LOGE(TAG, "Failed to initialize joystick");
        return;
    }
    ESP_LOGI(TAG, "✓ Joystick initialized");

    // Initialize WiFi and mDNS
    ESP_LOGI(TAG, "Initializing WiFi (%s)...", WIFI_SSID);
    if (wifi_init(WIFI_SSID, WIFI_PASSWORD) != 0) {
        ESP_LOGE(TAG, "Failed to initialize WiFi");
        return;
    }
    ESP_LOGI(TAG, "✓ WiFi initialization started");

    // Initialize mDNS early so it is ready when the STA gets an IP address.
    if (wifi_mdns_init(MDNS_HOSTNAME, WEB_SERVER_PORT) != 0)
    {
        ESP_LOGW(TAG, "mDNS startup failed, continuing without hostname discovery");
    }

    // Wait for WiFi connection
    int retry_count = 0;
    while (!wifi_is_connected() && retry_count < 50) {
        vTaskDelay(pdMS_TO_TICKS(100));
        retry_count++;
    }

    if (wifi_is_connected()) {
        ESP_LOGI(TAG, "✓ WiFi connected: %s", wifi_get_local_ip());
    } else {
        ESP_LOGW(TAG, "⚠ WiFi connection timeout, proceeding with ESP-NOW only");
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

    // Initialize Web Server
    ESP_LOGI(TAG, "Initializing Web Server on port %d...", WEB_SERVER_PORT);
    if (web_server_init(WEB_SERVER_PORT) == 0) {
        ESP_LOGI(TAG, "✓ Web Server started");
        if (wifi_is_connected()) {
            ESP_LOGI(TAG, "  Access at: http://%s or http://transmeter.local",
                     wifi_get_local_ip());
        }
    } else {
        ESP_LOGW(TAG, "⚠ Failed to initialize web server");
    }

    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "System initialization complete!");
    ESP_LOGI(TAG, "Ready to transmit motor control commands");
    ESP_LOGI(TAG, "==========================================================");

    // Create tasks on CPU 1 so CPU 0 can service WiFi/LwIP and idle watchdog.
    xTaskCreatePinnedToCore(remote_control_task, "RemoteCtrl", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(wifi_monitor_task, "WiFiMonitor", 2048, NULL, 3, NULL, 1);

    ESP_LOGI(TAG, "All tasks created successfully");
}
