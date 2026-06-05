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
#include "lwip/lwip_napt.h"
#include "lwip/udp.h" // Required for custom PCB listener tracking
#include "dhcpserver/dhcpserver_options.h"
#include "usb/usb_host.h"

#include "iot_usbh_rndis.h"
#include "iot_eth.h"
#include "iot_eth_netif_glue.h"
#include "iot_usbh_cdc.h"

static const char *TAG = "WIFI_USB_BRIDGE";

/* WiFi Configuration */
#define WIFI_SSID "SLEngineers"
#define WIFI_PASS "slengnet1"
#define MAX_RETRY 5

/* RNDIS Subnet Configuration (172.32.0.x) */
#define RNDIS_IP_A 172
#define RNDIS_IP_B 32
#define RNDIS_IP_C 0
#define RNDIS_IP_D 1

/* Target Luckfox Board WireGuard Details */
#define WG_UDP_PORT 51820 // WireGuard default listen port

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
static int s_retry_num = 0;

// Dynamic NAT Tracking variables for the WireGuard Peer
static ip_addr_t dynamic_vps_ip = IPADDR4_INIT_BYTES(0, 0, 0, 0);
static uint16_t dynamic_vps_port = 0;
static bool vps_endpoint_resolved = false;

/**
 * Active Network Bridge Callback
 * Discriminates between incoming WAN traffic and outgoing Luckfox traffic,
 * dynamically tracking endpoint mappings to keep the tunnel open.
 */
void wireguard_udp_forward_callback(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                                    const ip_addr_t *addr, u16_t port)
{
    if (p != NULL)
    {
        uint32_t incoming_ip = ip4_addr_get_u32(ip_2_ip4(addr));

        // Define our Luckfox board static IP representation
        ip_addr_t target_luckfox_ip;
        IP_ADDR4(&target_luckfox_ip, 172, 32, 0, 93);
        uint32_t luckfox_raw_ip = ip4_addr_get_u32(ip_2_ip4(&target_luckfox_ip));

        // CASE 1: Outbound packet from the Luckfox board destined for the internet
        if (incoming_ip == luckfox_raw_ip)
        {
            if (vps_endpoint_resolved)
            {
                // Route the response out to the tracked remote peer's public IP and port
                err_t send_err = udp_sendto(pcb, p, &dynamic_vps_ip, dynamic_vps_port);
                if (send_err != ERR_OK)
                {
                    ESP_LOGE("WG_BRIDGE", "Outbound WAN send failed: %d", send_err);
                }
            }
            else
            {
                ESP_LOGW("WG_BRIDGE", "Dropping outbound packet: Remote client endpoint not yet resolved.");
            }
        }
        // CASE 2: Inbound packet arriving from the internet
        else
        {
            // Dynamically save/update the external peer's mapping
            dynamic_vps_ip = *addr;
            dynamic_vps_port = port;
            if (!vps_endpoint_resolved)
            {
                vps_endpoint_resolved = true;
                ESP_LOGI("WG_BRIDGE", "Mapped external peer endpoint to " IPSTR ":%d",
                         IP2STR(ip_2_ip4(&dynamic_vps_ip)), dynamic_vps_port);
            }

            // Forward the packet downstream over the RNDIS link to Luckfox
            err_t send_err = udp_sendto(pcb, p, &target_luckfox_ip, WG_UDP_PORT);
            if (send_err != ERR_OK)
            {
                ESP_LOGE("WG_BRIDGE", "Inbound RNDIS forward failed: %d", send_err);
            }
        }

        // Free the buffer allocation cleanly
        pbuf_free(p);
    }
}

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
    // 1. Core System Init
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_wifi_event_group = xEventGroupCreate();

    // 2. Wi-Fi Station Initialization
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

    // 3. Enable NAPT
    esp_netif_ip_info_t sta_ip;
    ESP_ERROR_CHECK(esp_netif_get_ip_info(sta_netif, &sta_ip));

    // Enable NAPT on the interface connected to internet (Wi-Fi Station)
    ip_napt_enable(sta_ip.ip.addr, 1);
    ESP_LOGI(TAG, "NAPT fully initialized on STA interface");

    // -------------------------------------------------------------------------
    // Active Inbound WireGuard Forwarding Engine Configuration
    // -------------------------------------------------------------------------
    struct udp_pcb *wg_forward_pcb = udp_new();
    if (wg_forward_pcb != NULL)
    {
        err_t err = udp_bind(wg_forward_pcb, IP_ADDR_ANY, WG_UDP_PORT);
        if (err == ERR_OK)
        {
            // Attach structural ingestion callback to forward intercepted packets
            udp_recv(wg_forward_pcb, wireguard_udp_forward_callback, NULL);
            ESP_LOGI(TAG, "Active UDP WireGuard Forwarder bound on port %d", WG_UDP_PORT);
        }
        else
        {
            ESP_LOGE(TAG, "Failed to bind active UDP forwarder: %d", err);
        }
    }

    // 4. USB Host CDC Driver Install
    usbh_cdc_driver_config_t cdc_cfg = {
        .task_stack_size = 4096,
        .task_priority = 5,
        .task_coreid = 0,
        .skip_init_usb_host_driver = false,
    };
    ESP_ERROR_CHECK(usbh_cdc_driver_install(&cdc_cfg));

    // 5. RNDIS Driver Initialization
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

    // 6. Netif Creation for USB RNDIS
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

    // 7. Static IP + DHCP Server with DNS
    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, RNDIS_IP_A, RNDIS_IP_B, RNDIS_IP_C, RNDIS_IP_D);
    IP4_ADDR(&ip_info.gw, RNDIS_IP_A, RNDIS_IP_B, RNDIS_IP_C, RNDIS_IP_D);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);

    esp_netif_dhcpc_stop(rndis_netif);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(rndis_netif, &ip_info));

    // Push Google DNS to downstream board automatically
    esp_netif_dns_info_t dns = {
        .ip = ESP_IP4ADDR_INIT(8, 8, 8, 8),
    };
    ESP_ERROR_CHECK(esp_netif_set_dns_info(rndis_netif, ESP_NETIF_DNS_MAIN, &dns));

    ESP_ERROR_CHECK(esp_netif_dhcps_start(rndis_netif));

    ESP_LOGI(TAG, "Bridge ready at 172.32.0.1. DNS: 8.8.8.8 pushed via DHCP.");

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}