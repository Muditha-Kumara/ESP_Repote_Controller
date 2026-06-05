idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES 
                           esp_wifi 
                           esp_event 
                           nvs_flash 
                           esp_netif 
                           lwip 
                           usb 
                           esp_timer
                       PRIV_REQUIRES
                           iot_usbh_cdc
                           iot_usbh_rndis
                           iot_eth)
target_compile_definitions(${COMPONENT_TARGET} PRIVATE -DLWIP_NAPT_PORTMAP_MAX=16)