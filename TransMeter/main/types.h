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
