idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES freertos esp_wifi esp_event nvs_flash esp_netif lwip
                    PRIV_REQUIRES iot_usbh_rndis iot_eth iot_usbh_cdc)