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
 * Update AP/network state for display.
 * @param settings: Pointer to remote_settings_t structure
 * @param ap_channel: SoftAP channel in use
 */
void web_server_update_network_state(const remote_settings_t *settings, uint8_t ap_channel);

/**
 * Update motor control status for display
 * @param motor_data: Pointer to motor_control_t structure
 */
void web_server_update_motor_data(const motor_control_t *motor_data);

/**
 * Update joystick state for display.
 */
void web_server_update_joystick_state(uint8_t connected, uint8_t tx_enabled, int16_t lx, int16_t ly);

/**
 * Update packet count for display.
 */
void web_server_update_packet_count(uint32_t packet_count);

/**
 * Update latest link metrics received from receiver telemetry
 */
void web_server_update_link_metrics(int8_t rssi_dbm, float distance_m, uint32_t rtt_us, uint8_t valid);

/**
 * Update drive command received from the web interface
 */
void web_server_update_drive_command(int16_t throttle, int16_t steer, uint8_t armed);

/**
 * Copy the latest motor command state for transmission
 */
void web_server_get_motor_data(motor_control_t *motor_data);

/**
 * Stop web server
 */
void web_server_stop(void);

#endif // WEB_SERVER_H
