#include "tb6612fng.h"

#include <stdlib.h>
#include <stdbool.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "TB6612FNG";
static bool s_driver_stopped = false;

// Dual thrust motor mapping.
// motor1 -> left motor channel, motor2 -> right motor channel.
#define LEFT_PWM_GPIO GPIO_NUM_21
#define LEFT_IN1_GPIO GPIO_NUM_18
#define LEFT_IN2_GPIO GPIO_NUM_19
#define RIGHT_PWM_GPIO GPIO_NUM_22
#define RIGHT_IN1_GPIO GPIO_NUM_16
#define RIGHT_IN2_GPIO GPIO_NUM_17
#define TB6612_STBY_GPIO  GPIO_NUM_23

#define PWM_FREQ_HZ       20000
#define PWM_RESOLUTION    LEDC_TIMER_10_BIT
#define PWM_MAX_DUTY ((1U << 10) - 1U)

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
    int magnitude = abs((int)speed);
    int signed_speed = 0;

    if (magnitude > 127)
    {
        magnitude = 127;
    }

    // Follow direction field strictly: -1 reverse, 0 stop, +1 forward.
    if (direction < 0) {
        signed_speed = -magnitude;
    } else if (direction > 0) {
        signed_speed = magnitude;
    }
    else
    {
        signed_speed = 0;
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
        .pin_bit_mask = (1ULL << LEFT_IN1_GPIO) |
                        (1ULL << LEFT_IN2_GPIO) |
                        (1ULL << RIGHT_IN1_GPIO) |
                        (1ULL << RIGHT_IN2_GPIO) |
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

    ledc_channel_config_t left_channel = {
        .gpio_num = LEFT_PWM_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&left_channel));

    ledc_channel_config_t right_channel = {
        .gpio_num = RIGHT_PWM_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&right_channel));

    gpio_set_level(TB6612_STBY_GPIO, 1);
    tb6612fng_stop();

    ESP_LOGI(TAG,
             "TB6612FNG ready: left PWM=%d IN1=%d IN2=%d, right PWM=%d IN1=%d IN2=%d, STBY=%d",
             LEFT_PWM_GPIO, LEFT_IN1_GPIO, LEFT_IN2_GPIO,
             RIGHT_PWM_GPIO, RIGHT_IN1_GPIO, RIGHT_IN2_GPIO,
             TB6612_STBY_GPIO);
}

void tb6612fng_apply(const motor_control_t *command)
{
    if (command == NULL) {
        tb6612fng_stop();
        return;
    }

    gpio_set_level(TB6612_STBY_GPIO, 1);
    s_driver_stopped = false;

    int left_motor_speed = signed_speed_from_command(command->motor1_speed, command->motor1_direction);
    int right_motor_speed = signed_speed_from_command(command->motor2_speed, command->motor2_direction);

    ESP_LOGI(TAG, "Applied motor speeds: left=%d, right=%d", left_motor_speed, right_motor_speed);

    apply_motor(LEDC_CHANNEL_0, LEFT_IN1_GPIO, LEFT_IN2_GPIO, left_motor_speed);
    apply_motor(LEDC_CHANNEL_1, RIGHT_IN1_GPIO, RIGHT_IN2_GPIO, right_motor_speed);
}

void tb6612fng_stop(void)
{
    if (s_driver_stopped)
    {
        return;
    }

    ESP_LOGI(TAG, "Applied motor speeds: left=0, right=0");
    set_pwm(LEDC_CHANNEL_0, 0);
    set_pwm(LEDC_CHANNEL_1, 0);
    set_direction(LEFT_IN1_GPIO, LEFT_IN2_GPIO, 0);
    set_direction(RIGHT_IN1_GPIO, RIGHT_IN2_GPIO, 0);
    gpio_set_level(TB6612_STBY_GPIO, 0);
    s_driver_stopped = true;
}
