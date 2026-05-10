#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#include <stdint.h>

/**
 * Initialize WiFi connection with provided credentials
 * @param ssid: WiFi SSID
 * @param password: WiFi password
 * @param ap_ssid: SoftAP SSID used when router access is unavailable
 * @param ap_password: SoftAP password
 * @return 0 on success, -1 on error
 */
int wifi_init(const char *ssid, const char *password, const char *ap_ssid, const char *ap_password);

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
 * Get SoftAP IP address
 * @return AP IP string (static buffer, do not free)
 */
const char *wifi_get_ap_ip(void);

/**
 * Get SoftAP SSID
 * @return AP SSID string (static buffer, do not free)
 */
const char *wifi_get_ap_ssid(void);

/**
 * Check whether SoftAP is running
 * @return 1 if active, 0 otherwise
 */
int wifi_is_ap_active(void);

/**
 * Deinitialize WiFi and mDNS
 */
void wifi_config_deinit(void);

#endif // WIFI_CONFIG_H
