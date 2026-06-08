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
#include "lwip/udp.h"
#include "lwip/ip4.h"
#include "lwip/inet_chksum.h"
#include "lwip/ip_addr.h"
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

/* RNDIS Subnet Configuration */
#define RNDIS_IP_A 172
#define RNDIS_IP_B 32
#define RNDIS_IP_C 0
#define RNDIS_IP_D 1

/* Luckfox board static IP (four separate bytes) */
#define LUCKFOX_IP1 172
#define LUCKFOX_IP2 32
#define LUCKFOX_IP3 0
#define LUCKFOX_IP4 93
#define WG_UDP_PORT 51820

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
static int s_retry_num = 0;

/**
 * Extract destination IP and port from a UDP packet inside a pbuf.
 * Handles chained pbufs by copying headers into a temporary buffer.
 */
static bool get_udp_destination(struct pbuf *p, ip_addr_t *dest_ip, uint16_t *dest_port)
{
    struct ip_hdr iphdr;
    struct udp_hdr udphdr;
    uint16_t iphdr_len;
    size_t offset = 0;

    // Copy IP header (minimum 20 bytes)
    if (pbuf_copy_partial(p, &iphdr, sizeof(iphdr), offset) < sizeof(iphdr))
    {
        return false;
    }
    if (IPH_V(&iphdr) != 4)
    {
        return false; // Only IPv4 supported
    }
    iphdr_len = IPH_HL(&iphdr) * 4;
    if (iphdr_len < sizeof(iphdr))
    {
        return false;
    }

    // Copy UDP header
    offset = iphdr_len;
    if (pbuf_copy_partial(p, &udphdr, sizeof(udphdr), offset) < sizeof(udphdr))
    {
        return false;
    }

    // Set destination IP address (IPv4)
    ip_addr_set_ip4_u32(dest_ip, iphdr.dest.addr);
    *dest_port = lwip_ntohs(udphdr.dest);
    return true;
}

/**
 * Active Network Bridge Callback – handles all WireGuard traffic.
 * No per‑client state => unlimited concurrent peers.
 */
void wireguard_udp_forward_callback(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                                    const ip_addr_t *src_addr, u16_t src_port)
{
    if (p == NULL)
        return;

    // Luckfox IP address – defined once as a constant
    static const ip_addr_t luckfox_ip = IPADDR4_INIT_BYTES(LUCKFOX_IP1, LUCKFOX_IP2, LUCKFOX_IP3, LUCKFOX_IP4);

    // Check if packet originated from the Luckfox board
    if (ip_addr_cmp(src_addr, &luckfox_ip))
    {
        // Outbound packet (Luckfox → Internet) → forward to its destination
        ip_addr_t dest_ip;
        uint16_t dest_port;
        if (get_udp_destination(p, &dest_ip, &dest_port))
        {
            ESP_LOGD("WG_BRIDGE", "Outbound to %d.%d.%d.%d:%d",
                     ip4_addr1_16(ip_2_ip4(&dest_ip)),
                     ip4_addr2_16(ip_2_ip4(&dest_ip)),
                     ip4_addr3_16(ip_2_ip4(&dest_ip)),
                     ip4_addr4_16(ip_2_ip4(&dest_ip)),
                     dest_port);
            err_t err = udp_sendto(pcb, p, &dest_ip, dest_port);
            if (err != ERR_OK)
            {
                ESP_LOGE("WG_BRIDGE", "Outbound send failed: %d", err);
            }
        }
        else
        {
            ESP_LOGW("WG_BRIDGE", "Failed to parse outbound UDP header");
        }
    }
    else
    {
        // Inbound packet (Internet → ESP32) → forward to Luckfox
        ESP_LOGI("WG_BRIDGE", "Inbound from %d.%d.%d.%d:%d",
                 ip4_addr1_16(ip_2_ip4(src_addr)),
                 ip4_addr2_16(ip_2_ip4(src_addr)),
                 ip4_addr3_16(ip_2_ip4(src_addr)),
                 ip4_addr4_16(ip_2_ip4(src_addr)),
                 src_port);
        err_t err = udp_sendto(pcb, p, &luckfox_ip, WG_UDP_PORT);
        if (err != ERR_OK)
        {
            ESP_LOGE("WG_BRIDGE", "Inbound forward failed: %d", err);
        }
    }
    pbuf_free(p);
}

/* Wi-Fi event handler (unchanged) */
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
    // 1. Core system init
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_wifi_event_group = xEventGroupCreate();

    // 2. Wi-Fi station
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

    // 3. Enable NAPT (general internet sharing)
    esp_netif_ip_info_t sta_ip;
    ESP_ERROR_CHECK(esp_netif_get_ip_info(sta_netif, &sta_ip));
    ip_napt_enable(sta_ip.ip.addr, 1);
    ESP_LOGI(TAG, "NAPT enabled on STA interface");

    // 4. Create custom UDP forwarder on port 51820 (handles all peers)
    struct udp_pcb *wg_forward_pcb = udp_new();
    if (wg_forward_pcb)
    {
        err_t err = udp_bind(wg_forward_pcb, IP_ADDR_ANY, WG_UDP_PORT);
        if (err == ERR_OK)
        {
            udp_recv(wg_forward_pcb, wireguard_udp_forward_callback, NULL);
            ESP_LOGI(TAG, "UDP forwarder listening on port %d (multiple peers supported)", WG_UDP_PORT);
        }
        else
        {
            ESP_LOGE(TAG, "Failed to bind UDP forwarder: %d", err);
        }
    }

    // 5. USB Host CDC + RNDIS (unchanged)
    usbh_cdc_driver_config_t cdc_cfg = {
        .task_stack_size = 4096,
        .task_priority = 5,
        .task_coreid = 0,
        .skip_init_usb_host_driver = false,
    };
    ESP_ERROR_CHECK(usbh_cdc_driver_install(&cdc_cfg));

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

    // 6. Netif for RNDIS
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

    // 7. Static IP + DHCP server on RNDIS
    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, RNDIS_IP_A, RNDIS_IP_B, RNDIS_IP_C, RNDIS_IP_D);
    IP4_ADDR(&ip_info.gw, RNDIS_IP_A, RNDIS_IP_B, RNDIS_IP_C, RNDIS_IP_D);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);

    esp_netif_dhcpc_stop(rndis_netif);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(rndis_netif, &ip_info));

    esp_netif_dns_info_t dns = {.ip.u_addr.ip4.addr = ipaddr_addr("8.8.8.8")};
    ESP_ERROR_CHECK(esp_netif_set_dns_info(rndis_netif, ESP_NETIF_DNS_MAIN, &dns));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(rndis_netif));

    ESP_LOGI(TAG, "Bridge ready: ESP32 (Wi-Fi) <-> Luckfox (%d.%d.%d.%d)",
             LUCKFOX_IP1, LUCKFOX_IP2, LUCKFOX_IP3, LUCKFOX_IP4);
    ESP_LOGI(TAG, "WireGuard port %d forwarded to Luckfox. Multiple peers supported.", WG_UDP_PORT);

    // 8. Idle loop – everything runs in callbacks
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(30000));
        ESP_LOGI(TAG, "Bridge alive – no per‑peer state needed, all packets forwarded dynamically.");
    }
}