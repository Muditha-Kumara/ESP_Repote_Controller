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

#endif // TYPES_H
