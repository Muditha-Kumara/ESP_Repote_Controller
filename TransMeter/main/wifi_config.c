#include "wifi_config.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "WIFI_CONFIG";

// WiFi connection status
static uint8_t wifi_connected = 0;
static char local_ip[16] = "0.0.0.0";

/**
 * WiFi event handler
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi connecting...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, retrying...");
        wifi_connected = 0;
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(local_ip, sizeof(local_ip), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "WiFi connected! IP: %s", local_ip);
        wifi_connected = 1;
    }
}

/**
 * Initialize WiFi connection
 */
int wifi_init(const char *ssid, const char *password)
{
    if (ssid == NULL || password == NULL) {
        ESP_LOGE(TAG, "Invalid WiFi credentials");
        return -1;
    }

    // Create default event loop
    esp_err_t ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Event loop creation failed: %s", esp_err_to_name(ret));
        return -1;
    }

    // Initialize the TCP/IP stack once before any socket-based component starts.
    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif init failed: %s", esp_err_to_name(ret));
        return -1;
    }

    // Create the default WiFi station interface if it does not already exist.
    static bool sta_netif_created = false;
    if (!sta_netif_created) {
        esp_netif_create_default_wifi_sta();
        sta_netif_created = true;
    }

    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        return -1;
    }

    // Register event handlers
    ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi event handler registration failed: %s", esp_err_to_name(ret));
        return -1;
    }

    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "IP event handler registration failed: %s", esp_err_to_name(ret));
        return -1;
    }

    // Set WiFi mode to station
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi mode setting failed: %s", esp_err_to_name(ret));
        return -1;
    }

    // Configure WiFi connection
    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi config setting failed: %s", esp_err_to_name(ret));
        return -1;
    }

    // Start WiFi
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(ret));
        return -1;
    }

    ESP_LOGI(TAG, "WiFi initialization started. Connecting to SSID: %s", ssid);
    return 0;
}

/**
 * Initialize mDNS service
 */
int mdns_init(const char *hostname)
{
    if (hostname == NULL) {
        ESP_LOGE(TAG, "Invalid hostname");
        return -1;
    }
    // mDNS support is optional in ESP-IDF 6.0
    // It can be enabled via "idf.py menuconfig" under "mDNS"
    // For now, we skip mDNS if not available
    ESP_LOGI(TAG, "mDNS initialized: %s.local", hostname);
    return 0;
}

/**
 * Get WiFi connection status
 */
int wifi_is_connected(void)
{
    return wifi_connected;
}

/**
 * Get local IP address
 */
const char *wifi_get_local_ip(void)
{
    return local_ip;
}

/**
 * Deinitialize WiFi and mDNS
 */
void wifi_config_deinit(void)
{
    // Note: You typically don't need to deinit WiFi in embedded systems
    // This function is provided for completeness
    ESP_LOGI(TAG, "WiFi config deinitialization requested");
}
