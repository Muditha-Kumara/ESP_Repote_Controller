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
#define WIFI_SSID "SLEngineers"
#define WIFI_PASSWORD "slengnet1"
#define AP_SSID "Binaru"
#define AP_PASSWORD "binaru123"
#define MDNS_HOSTNAME "Binaru"
#define WIFI_CHANNEL 11

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
    int16_t lx = 0;
    int16_t ly = 0;
    TickType_t last_wake_time = xTaskGetTickCount();

    while (1) {
        memset(&motor_data, 0, sizeof(motor_data));

        if (joystick_is_connected())
        {
            joystick_get_raw_values(&lx, &ly);
            if (joystick_read(&motor_data) != 0)
            {
                ESP_LOGW(TAG, "Failed to read joystick state");
            }
        }
        else
        {
            lx = 0;
            ly = 0;
        }
        motor_data.timestamp = esp_log_timestamp();

        web_server_update_joystick_state(joystick_is_connected(), joystick_is_tx_enabled(), lx, ly);
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

        web_server_update_packet_count(packet_count);

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
    ESP_LOGI(TAG, "Control Mode: Read-only boat dashboard");
    ESP_LOGI(TAG, "Build Date: %s %s", __DATE__, __TIME__);

    // Initialize NVS (Non-Volatile Storage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI(TAG, "Erasing NVS flash");
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Initialize SoftAP and mDNS
    ESP_LOGI(TAG, "Initializing SoftAP (%s)...", AP_SSID);
    if (wifi_init(NULL, NULL, AP_SSID, AP_PASSWORD) != 0) {
        ESP_LOGE(TAG, "Failed to initialize WiFi");
        return;
    }
    ESP_LOGI(TAG, "✓ SoftAP initialization started");

    remote_settings_t settings = {
        .long_range_enabled = 1,
        .power_level = 0,
        .send_interval_ms = SEND_INTERVAL_MS,
    };
    snprintf(settings.wifi_ssid, sizeof(settings.wifi_ssid), "%s", AP_SSID);
    snprintf(settings.wifi_password, sizeof(settings.wifi_password), "%s", AP_PASSWORD);
    web_server_update_network_state(&settings, WIFI_CHANNEL);

    // Initialize mDNS early so it is ready when the STA gets an IP address.
    if (wifi_mdns_init(MDNS_HOSTNAME, WEB_SERVER_PORT) != 0)
    {
        ESP_LOGW(TAG, "mDNS startup failed, continuing without hostname discovery");
    }

    if (wifi_is_connected()) {
        ESP_LOGI(TAG, "✓ SoftAP active: %s", wifi_get_ap_ip());
    } else {
        ESP_LOGW(TAG, "⚠ SoftAP not reported active yet");
    }

    // Initialize ESP-NOW
    ESP_LOGI(TAG, "Initializing ESP-NOW on AP channel %d...", WIFI_CHANNEL);
    if (esp_now_tx_init(1, WIFI_CHANNEL) != 0) {
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
        web_server_update_network_state(&settings, WIFI_CHANNEL);
        ESP_LOGI(TAG, "  Access point: connect to SSID '%s' and open http://%s",
                 AP_SSID, wifi_get_ap_ip());
    } else {
        ESP_LOGW(TAG, "⚠ Failed to initialize web server");
    }

    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "System initialization complete!");
    ESP_LOGI(TAG, "Ready for live telemetry dashboard and ESP-NOW control");
    ESP_LOGI(TAG, "==========================================================");

    // Create tasks on CPU 1 so CPU 0 can service WiFi/LwIP and idle watchdog.
    xTaskCreatePinnedToCore(remote_control_task, "RemoteCtrl", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(wifi_monitor_task, "WiFiMonitor", 2048, NULL, 3, NULL, 1);

    ESP_LOGI(TAG, "All tasks created successfully");
}
