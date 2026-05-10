#include "wifi_config.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "mdns.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "WIFI_CONFIG";

// WiFi connection status
static uint8_t wifi_connected = 0;
static char local_ip[16] = "0.0.0.0";
static char ap_ip[16] = "192.168.4.1";
static char ap_ssid[33] = "Binaru";
static esp_netif_t *sta_netif = NULL;
static esp_netif_t *ap_netif = NULL;
static uint8_t mdns_started = 0;
static uint8_t ap_started = 0;

/**
 * WiFi event handler
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi connecting...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, retrying...");
        wifi_connected = 0;
        if (mdns_started && sta_netif != NULL)
        {
            mdns_netif_action(sta_netif, MDNS_EVENT_DISABLE_IP4);
        }
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        ap_started = 1;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STOP) {
        ap_started = 0;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(local_ip, sizeof(local_ip), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "WiFi connected! IP: %s", local_ip);
        wifi_connected = 1;
        if (mdns_started && sta_netif != NULL)
        {
            mdns_netif_action(sta_netif, MDNS_EVENT_ANNOUNCE_IP4);
        }
    }
}

/**
 * Initialize WiFi connection
 */
int wifi_init(const char *ssid, const char *password, const char *ap_ssid_in, const char *ap_password)
{
    if (ssid == NULL || password == NULL || ap_ssid_in == NULL || ap_password == NULL) {
        ESP_LOGE(TAG, "Invalid WiFi credentials");
        return -1;
    }

    strncpy(ap_ssid, ap_ssid_in, sizeof(ap_ssid) - 1);
    ap_ssid[sizeof(ap_ssid) - 1] = '\0';

    // Create default event loop
    esp_err_t ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Event loop creation failed: %s", esp_err_to_name(ret));
        return -1;
    }

    // Initialize the TCP/IP stack once before any socket-based component starts.
    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif init failed: %s", esp_err_to_name(ret));
        return -1;
    }

    // Create the default WiFi station interface if it does not already exist.
    static bool sta_netif_created = false;
    if (!sta_netif_created) {
        sta_netif = esp_netif_create_default_wifi_sta();
        ap_netif = esp_netif_create_default_wifi_ap();
        sta_netif_created = true;
    }

    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        return -1;
    }

    // Register event handlers
    ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi event handler registration failed: %s", esp_err_to_name(ret));
        return -1;
    }

    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "IP event handler registration failed: %s", esp_err_to_name(ret));
        return -1;
    }

    wifi_config_t ap_config = {};
    strncpy((char *)ap_config.ap.ssid, ap_ssid, sizeof(ap_config.ap.ssid) - 1);
    strncpy((char *)ap_config.ap.password, ap_password, sizeof(ap_config.ap.password) - 1);
    ap_config.ap.ssid_len = strlen((const char *)ap_config.ap.ssid);
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = (strlen((const char *)ap_config.ap.password) > 0) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    if (ap_config.ap.authmode == WIFI_AUTH_WPA2_PSK && strlen((const char *)ap_config.ap.password) < 8) {
        ESP_LOGE(TAG, "SoftAP password must be at least 8 characters");
        return -1;
    }

    // Configure SoftAP first so it is available even if the router never comes up.
    ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi mode setting failed: %s", esp_err_to_name(ret));
        return -1;
    }

    ret = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SoftAP config failed: %s", esp_err_to_name(ret));
        return -1;
    }

    // Configure WiFi connection
    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi config setting failed: %s", esp_err_to_name(ret));
        return -1;
    }

    // Start WiFi
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(ret));
        return -1;
    }

    snprintf(ap_ip, sizeof(ap_ip), "192.168.4.1");

    ESP_LOGI(TAG, "WiFi initialization started. Connecting to SSID: %s", ssid);
    ESP_LOGI(TAG, "SoftAP fallback ready: %s (password set)", ap_ssid);
    return 0;
}

/**
 * Initialize mDNS service
 */
int wifi_mdns_init(const char *hostname, uint16_t port)
{
    if (hostname == NULL || hostname[0] == '\0' || port == 0)
    {
        ESP_LOGE(TAG, "Invalid hostname");
        return -1;
    }

    esp_err_t ret = mdns_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "mDNS init failed: %s", esp_err_to_name(ret));
        return -1;
    }

    mdns_started = 1;

    if (sta_netif != NULL)
    {
        ret = mdns_register_netif(sta_netif);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
        {
            ESP_LOGE(TAG, "Failed to register STA netif with mDNS: %s", esp_err_to_name(ret));
            return -1;
        }
    }

    ret = mdns_hostname_set(hostname);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set mDNS hostname: %s", esp_err_to_name(ret));
        return -1;
    }

    ret = mdns_instance_name_set("TransMeter");
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set mDNS instance name: %s", esp_err_to_name(ret));
        return -1;
    }

    ret = mdns_service_add(NULL, "_http", "_tcp", port, NULL, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "Failed to add mDNS HTTP service: %s", esp_err_to_name(ret));
        return -1;
    }

    if (sta_netif != NULL && wifi_connected)
    {
        mdns_netif_action(sta_netif, MDNS_EVENT_ENABLE_IP4);
        mdns_netif_action(sta_netif, MDNS_EVENT_ANNOUNCE_IP4);
    }

    ESP_LOGI(TAG, "mDNS initialized: %s.local", hostname);
    return 0;
}

/**
 * Get WiFi connection status
 */
int wifi_is_connected(void)
{
    return wifi_connected;
}

/**
 * Get local IP address
 */
const char *wifi_get_local_ip(void)
{
    return local_ip;
}

/**
 * Get SoftAP IP address
 */
const char *wifi_get_ap_ip(void)
{
    return ap_ip;
}

/**
 * Get SoftAP SSID
 */
const char *wifi_get_ap_ssid(void)
{
    return ap_ssid;
}

/**
 * Check whether SoftAP is running
 */
int wifi_is_ap_active(void)
{
    return ap_started;
}

/**
 * Deinitialize WiFi and mDNS
 */
void wifi_config_deinit(void)
{
    // Note: You typically don't need to deinit WiFi in embedded systems
    // This function is provided for completeness
    ESP_LOGI(TAG, "WiFi config deinitialization requested");
}
