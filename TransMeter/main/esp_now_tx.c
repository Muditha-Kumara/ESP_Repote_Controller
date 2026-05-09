#include "esp_now_tx.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ESP_NOW_TX";

// Statistics
static uint32_t tx_success_count = 0;
static uint32_t tx_fail_count = 0;
static uint8_t configured_peer_addr[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static bool peer_configured = false;

/**
 * ESP-NOW send callback
 */
static void esp_now_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    if (status == ESP_NOW_SEND_SUCCESS) {
        tx_success_count++;
        ESP_LOGD(TAG, "Packet sent successfully");
    } else {
        tx_fail_count++;
        ESP_LOGW(TAG, "Failed to send packet");
    }
}

/**
 * Initialize ESP-NOW protocol
 */
int esp_now_tx_init(uint8_t long_range_enabled, uint8_t wifi_channel)
{
    esp_err_t ret;
    
    // Ensure WiFi is initialized, but don't re-init if already done by other module
    wifi_mode_t current_mode;
    ret = esp_wifi_get_mode(&current_mode);
    if (ret == ESP_ERR_WIFI_NOT_INIT) {
        // Not initialized yet: initialize and start as STA
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ret = esp_wifi_init(&cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
            return -1;
        }

        ret = esp_wifi_set_mode(WIFI_MODE_STA);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "WiFi mode setting failed: %s", esp_err_to_name(ret));
            return -1;
        }

        ret = esp_wifi_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(ret));
            return -1;
        }
    } else if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi already initialized, skipping esp_wifi_init/start");
    } else {
        ESP_LOGW(TAG, "Unexpected return from esp_wifi_get_mode(): %s", esp_err_to_name(ret));
    }

    // Try to set WiFi channel to 1 for better ESP-NOW performance.
    // If this fails (e.g., STA is connecting/scanning), continue
    // because forcing the channel can be disruptive to an active STA.
    ret = esp_wifi_set_channel(wifi_channel, WIFI_SECOND_CHAN_NONE);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi channel setting failed: %s — continuing without forcing channel", esp_err_to_name(ret));
    }

    if (long_range_enabled)
    {
        ret = esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_LR);
        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "Failed to enable LR protocol: %s", esp_err_to_name(ret));
        }

        ret = esp_wifi_set_max_tx_power(80);
        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "Failed to set max TX power: %s", esp_err_to_name(ret));
        }
    }

    // Initialize ESP-NOW
    ret = esp_now_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW init failed: %s", esp_err_to_name(ret));
        return -1;
    }

    // Register send callback
    ret = esp_now_register_send_cb(esp_now_send_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW callback registration failed: %s", esp_err_to_name(ret));
        return -1;
    }

    // Enable long range mode if requested
    if (long_range_enabled) {
        // Long range mode will be set per peer using esp_now_set_peer_rate_config
        ESP_LOGI(TAG, "Long range mode (802.11b) will be configured per peer");
    }

    ESP_LOGI(TAG, "ESP-NOW initialized successfully");
    return 0;
}

/**
 * Register peer for transmission
 */
int esp_now_tx_add_peer(const uint8_t *peer_addr, uint8_t wifi_channel)
{
    esp_now_peer_info_t peer = {};

    memcpy(peer.peer_addr, peer_addr, ESP_NOW_ETH_ALEN);
    memcpy(configured_peer_addr, peer_addr, ESP_NOW_ETH_ALEN);
    peer_configured = true;
    peer.channel = wifi_channel;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;

    esp_err_t ret = esp_now_add_peer(&peer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add peer: %s", esp_err_to_name(ret));
        return -1;
    }

    // Note: Long range mode is configured at system level via esp_wifi_config_espnow_rate
    // or via WiFi PHY settings. Per-peer rate configuration may not be available.

    ESP_LOGI(TAG, "Peer added successfully");
    return 0;
}

/**
 * Send motor control data
 */
int esp_now_tx_send(const motor_control_t *data)
{
    if (data == NULL) {
        ESP_LOGE(TAG, "NULL data pointer");
        return -1;
    }

    if (!peer_configured) {
        ESP_LOGE(TAG, "No ESP-NOW peer configured");
        return -1;
    }

    esp_err_t ret = esp_now_send(configured_peer_addr, (uint8_t *)data, sizeof(motor_control_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW send failed: %s", esp_err_to_name(ret));
        return -1;
    }

    return 0;
}

/**
 * Get transmission statistics
 */
uint32_t esp_now_tx_get_stats(void)
{
    return tx_success_count;
}

/**
 * Deinitialize ESP-NOW
 */
void esp_now_tx_deinit(void)
{
    esp_now_deinit();
    esp_wifi_stop();
    esp_wifi_deinit();
    ESP_LOGI(TAG, "ESP-NOW deinitialized");
}
