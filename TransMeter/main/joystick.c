#include "joystick.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include <string.h>

static const char *TAG = "JOYSTICK";

// ADC handle and calibration
static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t adc_cali_handle = NULL;

// ADC channels
static adc_channel_t adc_x_ch = ADC_CHANNEL_0;
static adc_channel_t adc_y_ch = ADC_CHANNEL_3;
static adc_channel_t adc_x2_ch = ADC_CHANNEL_6;
static adc_channel_t adc_y2_ch = ADC_CHANNEL_7;

// Raw values cache
static uint16_t raw_x = 2048;
static uint16_t raw_y = 2048;
static uint16_t raw_x2 = 2048;
static uint16_t raw_y2 = 2048;

/**
 * Map ADC value to motor speed (-127 to 127)
 * Center point should be around 2048 for 12-bit ADC
 */
static int8_t map_to_speed(uint16_t adc_value)
{
    int16_t centered = (int16_t)adc_value - 2048;
    int8_t speed = (centered * 127) / 2048;
    
    // Dead zone: values close to center are treated as 0
    if (speed > -10 && speed < 10) {
        speed = 0;
    }
    
    return speed;
}

/**
 * Determine motor direction based on speed
 */
static int8_t get_direction(int8_t speed)
{
    if (speed > 0) return 1;      // Forward
    if (speed < 0) return -1;     // Reverse
    return 0;                      // Stop
}

/**
 * Initialize ADC for joystick inputs
 */
int joystick_init(int adc_ch_x, int adc_ch_y, int adc_ch_x2, int adc_ch_y2)
{
    adc_x_ch = (adc_channel_t)adc_ch_x;
    adc_y_ch = (adc_channel_t)adc_ch_y;
    adc_x2_ch = (adc_channel_t)adc_ch_x2;
    adc_y2_ch = (adc_channel_t)adc_ch_y2;

    // Initialize ADC1 oneshot unit
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC init failed: %s", esp_err_to_name(ret));
        return -1;
    }

    // Configure ADC channels with maximum attenuation
    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };

    ret = adc_oneshot_config_channel(adc_handle, adc_x_ch, &chan_config);
    if (ret != ESP_OK) return -1;
    
    ret = adc_oneshot_config_channel(adc_handle, adc_y_ch, &chan_config);
    if (ret != ESP_OK) return -1;
    
    ret = adc_oneshot_config_channel(adc_handle, adc_x2_ch, &chan_config);
    if (ret != ESP_OK) return -1;
    
    ret = adc_oneshot_config_channel(adc_handle, adc_y2_ch, &chan_config);
    if (ret != ESP_OK) return -1;

    // ADC calibration - optional
    // In ESP-IDF 6.0, calibration can be done but is not required for basic operation
    adc_cali_handle = NULL;  // Skip calibration for simplicity
    ESP_LOGI(TAG, "ADC ready (uncalibrated)");

    ESP_LOGI(TAG, "Joystick ADC initialized");
    return 0;
}

/**
 * Read joystick and update motor control data
 */
int joystick_read(motor_control_t *motor_data)
{
    if (motor_data == NULL) {
        ESP_LOGE(TAG, "NULL motor_data pointer");
        return -1;
    }

    if (adc_handle == NULL) {
        ESP_LOGE(TAG, "ADC not initialized");
        return -1;
    }

    // Read ADC channels
    adc_oneshot_read(adc_handle, adc_x_ch, (int *)&raw_x);
    adc_oneshot_read(adc_handle, adc_y_ch, (int *)&raw_y);
    adc_oneshot_read(adc_handle, adc_x2_ch, (int *)&raw_x2);
    adc_oneshot_read(adc_handle, adc_y2_ch, (int *)&raw_y2);

    // Map to motor speeds
    int8_t speed1 = map_to_speed(raw_y);   // Y axis controls speed
    int8_t speed2 = map_to_speed(raw_y2);

    // Update motor data
    motor_data->motor1_speed = speed1;
    motor_data->motor1_direction = get_direction(speed1);
    motor_data->motor2_speed = speed2;
    motor_data->motor2_direction = get_direction(speed2);
    motor_data->timestamp = esp_log_timestamp();

    return 0;
}

/**
 * Get raw ADC values for web display
 */
void joystick_get_raw_values(uint16_t *x, uint16_t *y, uint16_t *x2, uint16_t *y2)
{
    if (x) *x = raw_x;
    if (y) *y = raw_y;
    if (x2) *x2 = raw_x2;
    if (y2) *y2 = raw_y2;
}

/**
 * Deinitialize ADC
 */
void joystick_deinit(void)
{
    if (adc_handle) {
        adc_oneshot_del_unit(adc_handle);
    }
    ESP_LOGI(TAG, "Joystick deinitialized");
}
