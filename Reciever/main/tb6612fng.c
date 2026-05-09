#include "tb6612fng.h"

#include <stdlib.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "TB6612FNG";

// Receiver pin map constrained to the pins you provided.
// PWM and standby use the least risky GPIOs from the set.
#define THRUST_PWM_GPIO  GPIO_NUM_21
#define THRUST_IN1_GPIO   GPIO_NUM_18
#define THRUST_IN2_GPIO   GPIO_NUM_19
#define STEER_PWM_GPIO    GPIO_NUM_22
#define STEER_IN1_GPIO    GPIO_NUM_4
#define STEER_IN2_GPIO    GPIO_NUM_5
#define TB6612_STBY_GPIO  GPIO_NUM_23

#define PWM_FREQ_HZ       20000
#define PWM_RESOLUTION    LEDC_TIMER_10_BIT
#define PWM_MAX_DUTY      ((1U << 10) - 1U)

static void set_direction(gpio_num_t in1, gpio_num_t in2, int signed_speed)
{
    if (signed_speed > 0) {
        gpio_set_level(in1, 1);
        gpio_set_level(in2, 0);
    } else if (signed_speed < 0) {
        gpio_set_level(in1, 0);
        gpio_set_level(in2, 1);
    } else {
        gpio_set_level(in1, 0);
        gpio_set_level(in2, 0);
    }
}

static void set_pwm(ledc_channel_t channel, int signed_speed)
{
    uint32_t duty = (uint32_t)((abs(signed_speed) * PWM_MAX_DUTY) / 127);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}

static int signed_speed_from_command(int8_t speed, int8_t direction)
{
    int signed_speed = speed;

    if (direction < 0) {
        signed_speed = -abs(signed_speed);
    } else if (direction > 0) {
        signed_speed = abs(signed_speed);
    }

    if (signed_speed > 127) {
        signed_speed = 127;
    } else if (signed_speed < -127) {
        signed_speed = -127;
    }

    return signed_speed;
}

static void apply_motor(ledc_channel_t channel, gpio_num_t in1, gpio_num_t in2, int signed_speed)
{
    if (signed_speed == 0) {
        set_pwm(channel, 0);
        set_direction(in1, in2, 0);
        return;
    }

    set_direction(in1, in2, signed_speed);
    set_pwm(channel, signed_speed);
}

void tb6612fng_init(void)
{
    gpio_config_t out_config = {
        .pin_bit_mask = (1ULL << THRUST_IN1_GPIO) |
                        (1ULL << THRUST_IN2_GPIO) |
                        (1ULL << STEER_IN1_GPIO) |
                        (1ULL << STEER_IN2_GPIO) |
                        (1ULL << TB6612_STBY_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&out_config));

    gpio_set_level(TB6612_STBY_GPIO, 0);

    ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = PWM_RESOLUTION,
        .freq_hz = PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

    ledc_channel_config_t thrust_channel = {
        .gpio_num = THRUST_PWM_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&thrust_channel));

    ledc_channel_config_t steer_channel = {
        .gpio_num = STEER_PWM_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&steer_channel));

    gpio_set_level(TB6612_STBY_GPIO, 1);
    tb6612fng_stop();

    ESP_LOGI(TAG,
             "TB6612FNG ready: thrust PWM=%d IN1=%d IN2=%d, steer PWM=%d IN1=%d IN2=%d, STBY=%d",
             THRUST_PWM_GPIO, THRUST_IN1_GPIO, THRUST_IN2_GPIO,
             STEER_PWM_GPIO, STEER_IN1_GPIO, STEER_IN2_GPIO,
             TB6612_STBY_GPIO);
}

void tb6612fng_apply(const motor_control_t *command)
{
    if (command == NULL) {
        tb6612fng_stop();
        return;
    }

    gpio_set_level(TB6612_STBY_GPIO, 1);

    int thrust_speed = signed_speed_from_command(command->motor1_speed, command->motor1_direction);
    int steer_speed = signed_speed_from_command(command->motor2_speed, command->motor2_direction);

    apply_motor(LEDC_CHANNEL_0, THRUST_IN1_GPIO, THRUST_IN2_GPIO, thrust_speed);
    apply_motor(LEDC_CHANNEL_1, STEER_IN1_GPIO, STEER_IN2_GPIO, steer_speed);
}

void tb6612fng_stop(void)
{
    set_pwm(LEDC_CHANNEL_0, 0);
    set_pwm(LEDC_CHANNEL_1, 0);
    set_direction(THRUST_IN1_GPIO, THRUST_IN2_GPIO, 0);
    set_direction(STEER_IN1_GPIO, STEER_IN2_GPIO, 0);
    gpio_set_level(TB6612_STBY_GPIO, 0);
}
