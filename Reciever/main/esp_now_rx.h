#ifndef ESP_NOW_RX_H
#define ESP_NOW_RX_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "types.h"

esp_err_t esp_now_rx_init(void);
bool esp_now_rx_get_latest(motor_control_t *command);
uint32_t esp_now_rx_get_last_seen_ms(void);

#endif // ESP_NOW_RX_H
