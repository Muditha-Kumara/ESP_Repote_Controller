idf_component_register(SRCS "web_server.c" "joystick.c" "wifi_config.c" "esp_now_tx.c" "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_timer esp_wifi esp_event esp_netif nvs_flash mdns esp_adc esp_http_server)
