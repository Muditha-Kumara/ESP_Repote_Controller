#ifndef JOYSTICK_H
#define JOYSTICK_H

#include <stdbool.h>

#include "types.h"

/**
 * Initialize USB host joystick support.
 * Returns 0 on success, -1 on error.
 */
int joystick_init(void);

/**
 * Read the latest joystick state into the existing packet structure.
 * When no controller is connected or transmit is not enabled, the caller should transmit zeros.
 */
int joystick_read(motor_control_t *motor_data);

/**
 * Check whether an Xbox 360 USB controller is currently connected.
 */
bool joystick_is_connected(void);

/**
 * Get the latest raw lx/ly values from the controller.
 */
void joystick_get_raw_values(int16_t *lx, int16_t *ly);

/**
 * Deinitialize USB host joystick support.
 */
void joystick_deinit(void);

#endif // JOYSTICK_H
