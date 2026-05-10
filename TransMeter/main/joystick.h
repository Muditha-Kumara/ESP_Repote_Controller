#ifndef JOYSTICK_H
#define JOYSTICK_H

#include "types.h"

/**
 * Initialize ADC joystick inputs
 * @param adc_ch_x: ADC channel for X axis
 * @param adc_ch_y: ADC channel for Y axis
 * @param adc_ch_x2: ADC channel for second joystick X axis
 * @param adc_ch_y2: ADC channel for second joystick Y axis
 * @return 0 on success, -1 on error
 */
int joystick_init(int adc_ch_x, int adc_ch_y, int adc_ch_x2, int adc_ch_y2);

/**
 * Read joystick and update motor control data
 * @param motor_data: Pointer to motor_control_t structure to update
 * @return 0 on success, -1 on error
 */
int joystick_read(motor_control_t *motor_data);

/**
 * Get raw ADC values for web display
 * @param x: Pointer to store X value (0-4095)
 * @param y: Pointer to store Y value (0-4095)
 * @param x2: Pointer to store X2 value (0-4095)
 * @param y2: Pointer to store Y2 value (0-4095)
 */
void joystick_get_raw_values(uint16_t *x, uint16_t *y, uint16_t *x2, uint16_t *y2);

/**
 * Deinitialize ADC
 */
void joystick_deinit(void);

#endif // JOYSTICK_H
