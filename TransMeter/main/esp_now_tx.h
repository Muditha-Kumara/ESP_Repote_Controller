#ifndef ESP_NOW_TX_H
#define ESP_NOW_TX_H

#include "types.h"

typedef struct
{
    int8_t receiver_rssi_dbm;
    float estimated_distance_m;
    uint32_t round_trip_time_us;
    uint32_t last_update_ms;
    uint8_t valid;
} link_metrics_t;

/**
 * Initialize ESP-NOW protocol
 * @param long_range_enabled: Enable long range mode (802.11b mode)
 * @param wifi_channel: Fixed WiFi channel to use for ESP-NOW peers
 * @return 0 on success, -1 on error
 */
int esp_now_tx_init(uint8_t long_range_enabled, uint8_t wifi_channel);

/**
 * Register peer for transmission
 * @param peer_addr: MAC address of the receiver (6 bytes)
 * @param wifi_channel: Fixed WiFi channel to use for the peer
 * @return 0 on success, -1 on error
 */
int esp_now_tx_add_peer(const uint8_t *peer_addr, uint8_t wifi_channel);

/**
 * Send motor control data
 * @param data: Pointer to motor_control_t structure
 * @return 0 on success, -1 on error
 */
int esp_now_tx_send(const motor_control_t *data);

/**
 * Get last transmission status
 * @return Number of successful transmissions
 */
uint32_t esp_now_tx_get_stats(void);

/**
 * Get latest receiver link metrics (RSSI and RTT-based distance estimate)
 */
void esp_now_tx_get_link_metrics(link_metrics_t *metrics);

/**
 * Deinitialize ESP-NOW
 */
void esp_now_tx_deinit(void);

#endif // ESP_NOW_TX_H
