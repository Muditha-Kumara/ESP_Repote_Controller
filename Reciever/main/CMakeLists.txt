idf_component_register(
    SRCS
        "main.c"
        "esp_now_rx.c"
        "tb6612fng.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_gpio
        esp_driver_ledc
        esp_event
        esp_netif
        esp_timer
        esp_wifi
        nvs_flash
)
