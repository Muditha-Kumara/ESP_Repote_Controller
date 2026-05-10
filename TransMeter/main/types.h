#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>

/**
 * Motor control data structure
 * Contains speed and direction for two motors
 */
typedef struct {
    int8_t motor1_speed;      // Motor 1 speed: -127 to 127
    int8_t motor1_direction;  // Motor 1 direction: 0=stop, 1=forward, -1=reverse
    int8_t motor2_speed;      // Motor 2 speed: -127 to 127
    int8_t motor2_direction;  // Motor 2 direction: 0=stop, 1=forward, -1=reverse
    uint32_t timestamp;       // Packet timestamp
} motor_control_t;

/**
 * One-way telemetry packet sent by receiver back to transmitter.
 * This is additive and does not change motor_control_t packet behavior.
 */
typedef struct __attribute__((packed))
{
    uint16_t magic;               // "TM" marker to identify telemetry packets
    uint8_t version;              // Packet format version
    int8_t receiver_rssi_dbm;     // RSSI seen at receiver for last control packet
    uint32_t echoed_tx_timestamp; // Echo of motor_control_t.timestamp from transmitter
    uint32_t receiver_rx_time_us; // Receiver local RX timestamp (lower 32 bits)
} receiver_telemetry_t;

#define RECEIVER_TELEMETRY_MAGIC 0x4D54u
#define RECEIVER_TELEMETRY_VERSION 1u

/**
 * Remote settings structure for web interface
 */
typedef struct {
    uint8_t long_range_enabled;
    uint8_t power_level;
    uint32_t send_interval_ms;
    char wifi_ssid[32];
    char wifi_password[64];
} remote_settings_t;

#endif // TYPES_H
