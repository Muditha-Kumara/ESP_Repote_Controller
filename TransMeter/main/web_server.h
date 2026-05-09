#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "types.h"

/**
 * Initialize HTTP web server
 * @param port: HTTP port (default 80)
 * @return 0 on success, -1 on error
 */
int web_server_init(uint16_t port);

/**
 * Update remote settings for display
 * @param settings: Pointer to remote_settings_t structure
 */
void web_server_update_settings(const remote_settings_t *settings);

/**
 * Update motor control status for display
 * @param motor_data: Pointer to motor_control_t structure
 */
void web_server_update_motor_data(const motor_control_t *motor_data);

/**
 * Update ADC joystick readings for display
 */
void web_server_update_adc_values(uint16_t x, uint16_t y, uint16_t x2, uint16_t y2);

/**
 * Stop web server
 */
void web_server_stop(void);

#endif // WEB_SERVER_H
