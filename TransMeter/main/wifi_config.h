#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#include <stdint.h>

/**
 * Initialize WiFi connection with provided credentials
 * @param ssid: WiFi SSID
 * @param password: WiFi password
 * @return 0 on success, -1 on error
 */
int wifi_init(const char *ssid, const char *password);

/**
 * Initialize mDNS service
 * @param hostname: mDNS hostname (e.g., "transmeter")
 * @param port: HTTP service port to advertise
 * @return 0 on success, -1 on error
 */
int wifi_mdns_init(const char *hostname, uint16_t port);

/**
 * Get current WiFi connection status
 * @return 1 if connected, 0 if not connected
 */
int wifi_is_connected(void);

/**
 * Get local IP address
 * @return IP address string (static buffer, do not free)
 */
const char *wifi_get_local_ip(void);

/**
 * Deinitialize WiFi and mDNS
 */
void wifi_config_deinit(void);

#endif // WIFI_CONFIG_H
