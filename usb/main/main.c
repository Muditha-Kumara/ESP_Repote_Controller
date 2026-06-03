#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"

#include "lwip/opt.h"
#include "lwip/prot/ip.h"
#include "lwip/ip_addr.h"
#include "lwip/err.h"
#include "dhcpserver/dhcpserver_options.h"
#include "usb/usb_host.h"

#include "iot_usbh_rndis.h"
#include "iot_eth.h"
#include "iot_eth_netif_glue.h"
#include "iot_usbh_cdc.h"

/* ESP-IDF v6.0.1: NAPT functions exist in the compiled LwIP library
   but the public header may not declare them. Provide our own externs. */
extern void ip_napt_enable(u32_t addr, int enable);
extern err_t ip_portmap_add(u8_t proto, u32_t maddr, u16_t mport, u32_t daddr, u16_t dport);

#ifndef IP_PROTO_UDP
#define IP_PROTO_UDP 17
#endif

static const char *TAG = "WIFI_USB_BRIDGE";

/* WiFi Configuration */
#define WIFI_SSID "SLEngineers"
#define WIFI_PASS "slengnet1"
#define MAX_RETRY 5

/* RNDIS Network (USB side) */
#define RNDIS_IP_A 172
#define RNDIS_IP_B 32
#define RNDIS_IP_C 0
#define RNDIS_IP_D 1

/* Luckfox static IP — configure this manually on the Luckfox:
   ip addr add 172.32.0.93/24 dev usb0
   ip route add default via 172.32.0.1 dev usb0
*/
#define LUCKFOX_IP_A 172
#define LUCKFOX_IP_B 32
#define LUCKFOX_IP_C 0
#define LUCKFOX_IP_D 93

/* WireGuard port to forward from WAN to Luckfox */
#define WG_PORT 51820

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
static int s_retry_num = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < MAX_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Wi-Fi retry %d/%d", s_retry_num, MAX_RETRY);
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Wi-Fi got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void app_main(void)
{
    /* 1. Core init */
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_wifi_event_group = xEventGroupCreate();

    /* 2. Wi-Fi STA */
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Waiting for Wi-Fi...");
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    /* 3. Enable NAPT on STA interface */
    esp_netif_ip_info_t sta_ip;
    ESP_ERROR_CHECK(esp_netif_get_ip_info(sta_netif, &sta_ip));

    ip_napt_enable(sta_ip.ip.addr, 1);
    ESP_LOGI(TAG, "NAPT enabled on STA interface (%s)", esp_netif_get_desc(sta_netif));

    /* 4. USB Host CDC install */
    usbh_cdc_driver_config_t cdc_cfg = {
        .task_stack_size = 4096,
        .task_priority = 5,
        .task_coreid = 0,
        .skip_init_usb_host_driver = false,
    };
    ESP_ERROR_CHECK(usbh_cdc_driver_install(&cdc_cfg));

    /* 5. RNDIS driver */
    usb_device_match_id_t *dev_match_id = calloc(2, sizeof(usb_device_match_id_t));
    dev_match_id[0].match_flags = USB_DEVICE_ID_MATCH_VID_PID;
    dev_match_id[0].idVendor = USB_DEVICE_VENDOR_ANY;
    dev_match_id[0].idProduct = USB_DEVICE_PRODUCT_ANY;

    iot_usbh_rndis_config_t rndis_cfg = {.match_id_list = dev_match_id};
    iot_eth_driver_t *rndis_drv = NULL;
    ESP_ERROR_CHECK(iot_eth_new_usb_rndis(&rndis_cfg, &rndis_drv));

    iot_eth_config_t eth_cfg = {.driver = rndis_drv, .stack_input = NULL};
    iot_eth_handle_t eth_handle = NULL;
    ESP_ERROR_CHECK(iot_eth_install(&eth_cfg, &eth_handle));

    /* 6. Create netif for RNDIS */
    esp_netif_inherent_config_t inherent_eth_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
    inherent_eth_config.if_key = "USB_RNDIS";
    inherent_eth_config.if_desc = "USB_RNDIS";
    esp_netif_config_t netif_cfg = {
        .base = &inherent_eth_config,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
    };
    esp_netif_t *rndis_netif = esp_netif_new(&netif_cfg);

    iot_eth_netif_glue_handle_t glue = iot_eth_new_netif_glue(eth_handle);
    ESP_ERROR_CHECK(esp_netif_attach(rndis_netif, glue));
    ESP_ERROR_CHECK(iot_eth_start(eth_handle));

    /* 7. Configure RNDIS netif: static IP, DHCP server, DNS */
    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, RNDIS_IP_A, RNDIS_IP_B, RNDIS_IP_C, RNDIS_IP_D);
    IP4_ADDR(&ip_info.gw, RNDIS_IP_A, RNDIS_IP_B, RNDIS_IP_C, RNDIS_IP_D);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);

    ESP_ERROR_CHECK(esp_netif_dhcpc_stop(rndis_netif));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(rndis_netif, &ip_info));

    esp_netif_dns_info_t dns = {
        .ip = ESP_IP4ADDR_INIT(8, 8, 8, 8),
    };
    ESP_ERROR_CHECK(esp_netif_set_dns_info(rndis_netif, ESP_NETIF_DNS_MAIN, &dns));

    ESP_ERROR_CHECK(esp_netif_dhcps_start(rndis_netif));
    ESP_LOGI(TAG, "RNDIS bridge ready at %d.%d.%d.%d", RNDIS_IP_A, RNDIS_IP_B, RNDIS_IP_C, RNDIS_IP_D);

    /* 8. WireGuard port forward: WAN:51820 -> Luckfox:51820 */
    ip4_addr_t luckfox_ip;
    IP4_ADDR(&luckfox_ip, LUCKFOX_IP_A, LUCKFOX_IP_B, LUCKFOX_IP_C, LUCKFOX_IP_D);
    err_t err = ip_portmap_add(IP_PROTO_UDP, sta_ip.ip.addr, WG_PORT, luckfox_ip.addr, WG_PORT);
    if (err != ERR_OK)
    {
        ESP_LOGE(TAG, "WireGuard portmap FAILED (err=%d). Tables may be full or not compiled in.", err);
    }
    else
    {
        ESP_LOGI(TAG, "WireGuard UDP/%d forwarded to Luckfox %d.%d.%d.%d:%d",
                 WG_PORT, LUCKFOX_IP_A, LUCKFOX_IP_B, LUCKFOX_IP_C, LUCKFOX_IP_D, WG_PORT);
    }

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}