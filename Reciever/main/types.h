#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>

typedef struct {
    int8_t motor1_speed;
    int8_t motor1_direction;
    int8_t motor2_speed;
    int8_t motor2_direction;
    uint32_t timestamp;
} motor_control_t;

typedef struct __attribute__((packed))
{
    uint16_t magic;
    uint8_t version;
    int8_t receiver_rssi_dbm;
    uint32_t echoed_tx_timestamp;
    uint32_t receiver_rx_time_us;
} receiver_telemetry_t;

#define RECEIVER_TELEMETRY_MAGIC 0x4D54u
#define RECEIVER_TELEMETRY_VERSION 1u

#endif // TYPES_H
