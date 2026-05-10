#include "esp_now_tx.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include <string.h>

static const char *TAG = "ESP_NOW_TX";

// Statistics
static uint32_t tx_success_count = 0;
static uint32_t tx_fail_count = 0;
static uint8_t configured_peer_addr[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static bool peer_configured = false;

#define TX_HISTORY_SIZE 32

typedef struct
{
    uint32_t timestamp_ms;
    uint64_t sent_time_us;
} tx_history_entry_t;

static tx_history_entry_t tx_history[TX_HISTORY_SIZE] = {0};
static uint8_t tx_history_index = 0;

static link_metrics_t latest_metrics = {
    .receiver_rssi_dbm = 0,
    .estimated_distance_m = -1.0f,
    .round_trip_time_us = 0,
    .last_update_ms = 0,
    .valid = 0,
};
static portMUX_TYPE metrics_mux = portMUX_INITIALIZER_UNLOCKED;

static uint64_t find_sent_time_us(uint32_t echoed_timestamp_ms)
{
    for (int i = 0; i < TX_HISTORY_SIZE; ++i)
    {
        int idx = (int)tx_history_index - 1 - i;
        if (idx < 0)
        {
            idx += TX_HISTORY_SIZE;
        }
        if (tx_history[idx].timestamp_ms == echoed_timestamp_ms)
        {
            return tx_history[idx].sent_time_us;
        }
    }
    return 0;
}

static void record_sent_timestamp(uint32_t timestamp_ms)
{
    tx_history[tx_history_index].timestamp_ms = timestamp_ms;
    tx_history[tx_history_index].sent_time_us = (uint64_t)esp_timer_get_time();
    tx_history_index = (uint8_t)((tx_history_index + 1) % TX_HISTORY_SIZE);
}

/**
 * ESP-NOW send callback
 */
static void esp_now_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    (void)tx_info;
    if (status == ESP_NOW_SEND_SUCCESS) {
        tx_success_count++;
        ESP_LOGD(TAG, "Packet sent successfully");
    } else {
        tx_fail_count++;
        ESP_LOGW(TAG, "Failed to send packet");
    }
}

static void esp_now_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    (void)recv_info;

    if (data == NULL || len != (int)sizeof(receiver_telemetry_t))
    {
        return;
    }

    receiver_telemetry_t telemetry = {0};
    memcpy(&telemetry, data, sizeof(telemetry));

    if (telemetry.magic != RECEIVER_TELEMETRY_MAGIC || telemetry.version != RECEIVER_TELEMETRY_VERSION)
    {
        return;
    }

    const uint64_t now_us = (uint64_t)esp_timer_get_time();
    const uint64_t sent_us = find_sent_time_us(telemetry.echoed_tx_timestamp);
    uint32_t rtt_us = 0;
    float distance_m = -1.0f;

    if (sent_us > 0 && now_us > sent_us)
    {
        const uint64_t rtt_us_u64 = now_us - sent_us;
        if (rtt_us_u64 <= UINT32_MAX)
        {
            rtt_us = (uint32_t)rtt_us_u64;
        }
    }

    // Do not convert application-level RTT into distance. Accurate ranging
    // requires Wi-Fi FTM hardware timestamps and FTM-capable peers.
    (void)distance_m;
    distance_m = -1.0f;

    portENTER_CRITICAL(&metrics_mux);
    latest_metrics.receiver_rssi_dbm = telemetry.receiver_rssi_dbm;
    latest_metrics.round_trip_time_us = rtt_us;
    latest_metrics.estimated_distance_m = distance_m;
    latest_metrics.last_update_ms = (uint32_t)esp_log_timestamp();
    latest_metrics.valid = 1;
    portEXIT_CRITICAL(&metrics_mux);
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

    ret = esp_now_register_recv_cb(esp_now_recv_cb);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "ESP-NOW RX callback registration failed: %s", esp_err_to_name(ret));
        return -1;
    }

    ESP_LOGW(TAG, "Distance from ESP-NOW RTT is disabled. Use Wi-Fi FTM with FTM-capable peers for real ranging.");

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

    record_sent_timestamp(data->timestamp);

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

void esp_now_tx_get_link_metrics(link_metrics_t *metrics)
{
    if (metrics == NULL)
    {
        return;
    }

    portENTER_CRITICAL(&metrics_mux);
    memcpy(metrics, &latest_metrics, sizeof(*metrics));
    portEXIT_CRITICAL(&metrics_mux);
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
