#include "joystick.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

static const char *TAG = "JOYSTICK";

#define JOYSTICK_DEADZONE 10000

// Adjust this for the board you are using.
#define USB_VBUS_GPIO GPIO_NUM_10

// Xbox 360 controller reports 20 bytes in the format shown in the user sample.
typedef struct __attribute__((packed))
{
    uint8_t report_id;
    uint8_t packet_size;
    uint8_t buttons_low;
    uint8_t buttons_high;
    uint8_t lt;
    uint8_t rt;
    int16_t lx;
    int16_t ly;
    int16_t rx;
    int16_t ry;
    uint8_t reserved[6];
} xbox360_hid_report_t;

#define XBOX_BTN_BACK (1 << 5)
#define XBOX_BTN_START (1 << 4)
#define XBOX_DPAD_UP (1 << 0)
#define XBOX_DPAD_DOWN (1 << 1)
#define XBOX_DPAD_LEFT (1 << 2)
#define XBOX_DPAD_RIGHT (1 << 3)
#define XBOX_BTN_LS (1 << 6)
#define XBOX_BTN_RS (1 << 7)
#define XBOX_BTN_A (1 << 4)
#define XBOX_BTN_B (1 << 5)
#define XBOX_BTN_X (1 << 6)
#define XBOX_BTN_Y (1 << 7)
#define XBOX_BTN_LB (1 << 0)
#define XBOX_BTN_RB (1 << 1)
#define XBOX_BTN_XBOX (1 << 2)

static usb_host_client_handle_t client_hdl;
static usb_device_handle_t dev_hdl;
static usb_transfer_t *in_transfer;
static bool usb_ready = false;
static bool xbox_connected = false;
static bool joystick_tx_enabled = false;
static int8_t latest_lx = 0;
static int8_t latest_ly = 0;
static int8_t latest_left_motor = 0;
static int8_t latest_right_motor = 0;
static xbox360_hid_report_t prev_report = {0};

static void teleplot_send_i32(const char *series, int32_t value)
{
    printf(">%s:%ld|g\n", series, (long)value);
}

static int8_t axis_to_int8(int16_t value)
{
    if (value > -JOYSTICK_DEADZONE && value < JOYSTICK_DEADZONE)
    {
        return 0;
    }

    if (value > 0)
    {
        int32_t adjusted = (int32_t)value - JOYSTICK_DEADZONE;
        int32_t range = 32767 - JOYSTICK_DEADZONE;
        int32_t scaled = adjusted * 127 / range;
        if (scaled > 127)
        {
            scaled = 127;
        }
        return (int8_t)scaled;
    }

    int32_t adjusted = (int32_t)(-value) - JOYSTICK_DEADZONE;
    int32_t range = 32767 - JOYSTICK_DEADZONE;
    int32_t scaled = adjusted * 127 / range;
    if (scaled > 127)
    {
        scaled = 127;
    }
    return (int8_t)(-scaled);
}

static int8_t get_direction(int8_t speed)
{
    if (speed > 0)
    {
        return 1;
    }
    if (speed < 0)
    {
        return -1;
    }
    return 0;
}

static int8_t clamp_motor_value(int16_t value)
{
    if (value > 127)
    {
        return 127;
    }
    if (value < -127)
    {
        return -127;
    }
    return (int8_t)value;
}

static void mix_boat_motors(int8_t throttle, int8_t steer, int8_t *left_motor, int8_t *right_motor)
{
    int16_t left = (int16_t)throttle + (int16_t)steer;
    int16_t right = (int16_t)throttle - (int16_t)steer;

    if (left_motor != NULL)
    {
        *left_motor = clamp_motor_value(left);
    }
    if (right_motor != NULL)
    {
        *right_motor = clamp_motor_value(right);
    }
}

static void enable_vbus(void)
{
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << USB_VBUS_GPIO),
        .pull_down_en = 0,
        .pull_up_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config(&io_conf);
    gpio_set_level(USB_VBUS_GPIO, 1);
    ESP_LOGI(TAG, "VBUS enabled on GPIO %d", USB_VBUS_GPIO);
    vTaskDelay(pdMS_TO_TICKS(100));
}

static void hid_transfer_cb(usb_transfer_t *transfer)
{
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED)
    {
        xbox360_hid_report_t report = {0};
        memcpy(&report, transfer->data_buffer, sizeof(report));

        int8_t lx = axis_to_int8(report.lx);
        int8_t ly = axis_to_int8(report.ly);
        int8_t left_motor = 0;
        int8_t right_motor = 0;

        mix_boat_motors(ly, lx, &left_motor, &right_motor);

        if ((report.buttons_low ^ prev_report.buttons_low) != 0)
        {
            if (((report.buttons_low ^ prev_report.buttons_low) & XBOX_BTN_BACK) != 0)
            {
                teleplot_send_i32("xbox/back", (report.buttons_low & XBOX_BTN_BACK) ? 1 : 0);
            }
            if (((report.buttons_low ^ prev_report.buttons_low) & XBOX_BTN_START) != 0)
            {
                teleplot_send_i32("xbox/start", (report.buttons_low & XBOX_BTN_START) ? 1 : 0);
            }
            if (((report.buttons_low ^ prev_report.buttons_low) & XBOX_DPAD_UP) != 0)
            {
                teleplot_send_i32("xbox/dpad_up", (report.buttons_low & XBOX_DPAD_UP) ? 1 : 0);
            }
            if (((report.buttons_low ^ prev_report.buttons_low) & XBOX_DPAD_DOWN) != 0)
            {
                teleplot_send_i32("xbox/dpad_down", (report.buttons_low & XBOX_DPAD_DOWN) ? 1 : 0);
            }
            if (((report.buttons_low ^ prev_report.buttons_low) & XBOX_DPAD_LEFT) != 0)
            {
                teleplot_send_i32("xbox/dpad_left", (report.buttons_low & XBOX_DPAD_LEFT) ? 1 : 0);
            }
            if (((report.buttons_low ^ prev_report.buttons_low) & XBOX_DPAD_RIGHT) != 0)
            {
                teleplot_send_i32("xbox/dpad_right", (report.buttons_low & XBOX_DPAD_RIGHT) ? 1 : 0);
            }
            if (((report.buttons_low ^ prev_report.buttons_low) & XBOX_BTN_LS) != 0)
            {
                teleplot_send_i32("xbox/ls", (report.buttons_low & XBOX_BTN_LS) ? 1 : 0);
            }
            if (((report.buttons_low ^ prev_report.buttons_low) & XBOX_BTN_RS) != 0)
            {
                teleplot_send_i32("xbox/rs", (report.buttons_low & XBOX_BTN_RS) ? 1 : 0);
            }
        }

        if ((report.buttons_high ^ prev_report.buttons_high) != 0)
        {
            if (((report.buttons_high ^ prev_report.buttons_high) & XBOX_BTN_A) != 0)
            {
                teleplot_send_i32("xbox/a", (report.buttons_high & XBOX_BTN_A) ? 1 : 0);
            }
            if (((report.buttons_high ^ prev_report.buttons_high) & XBOX_BTN_B) != 0)
            {
                teleplot_send_i32("xbox/b", (report.buttons_high & XBOX_BTN_B) ? 1 : 0);
            }
            if (((report.buttons_high ^ prev_report.buttons_high) & XBOX_BTN_X) != 0)
            {
                teleplot_send_i32("xbox/x", (report.buttons_high & XBOX_BTN_X) ? 1 : 0);
            }
            if (((report.buttons_high ^ prev_report.buttons_high) & XBOX_BTN_Y) != 0)
            {
                teleplot_send_i32("xbox/y", (report.buttons_high & XBOX_BTN_Y) ? 1 : 0);
            }
            if (((report.buttons_high ^ prev_report.buttons_high) & XBOX_BTN_LB) != 0)
            {
                teleplot_send_i32("xbox/lb", (report.buttons_high & XBOX_BTN_LB) ? 1 : 0);
            }
            if (((report.buttons_high ^ prev_report.buttons_high) & XBOX_BTN_RB) != 0)
            {
                teleplot_send_i32("xbox/rb", (report.buttons_high & XBOX_BTN_RB) ? 1 : 0);
            }
            if (((report.buttons_high ^ prev_report.buttons_high) & XBOX_BTN_XBOX) != 0)
            {
                teleplot_send_i32("xbox/xbox", (report.buttons_high & XBOX_BTN_XBOX) ? 1 : 0);
            }
        }

        latest_lx = lx;
        latest_ly = ly;

        bool b_pressed = (report.buttons_high & XBOX_BTN_B) != 0;
        joystick_tx_enabled = b_pressed;

        if (!joystick_tx_enabled)
        {
            left_motor = 0;
            right_motor = 0;
        }

        latest_left_motor = left_motor;
        latest_right_motor = right_motor;

        if (lx != axis_to_int8(prev_report.lx))
        {
            teleplot_send_i32("xbox/lx", lx);
        }
        if (ly != axis_to_int8(prev_report.ly))
        {
            teleplot_send_i32("xbox/ly", ly);
        }

        if (report.lt != prev_report.lt)
        {
            teleplot_send_i32("xbox/lt", report.lt);
        }
        if (report.rt != prev_report.rt)
        {
            teleplot_send_i32("xbox/rt", report.rt);
        }

        teleplot_send_i32("xbox/b", b_pressed ? 1 : 0);

        prev_report = report;
    }
    else if (transfer->status != USB_TRANSFER_STATUS_NO_DEVICE)
    {
        ESP_LOGW(TAG, "HID transfer error: %d", transfer->status);
    }

    if (xbox_connected && in_transfer != NULL)
    {
        usb_host_transfer_submit(in_transfer);
    }
}

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    (void)arg;

    if (event_msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV)
    {
        ESP_LOGI(TAG, "New USB device detected at address %d", event_msg->new_dev.address);

        if (usb_host_device_open(client_hdl, event_msg->new_dev.address, &dev_hdl) != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to open USB device");
            return;
        }

        const usb_device_desc_t *dev_desc = NULL;
        if (usb_host_get_device_descriptor(dev_hdl, &dev_desc) != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to read device descriptor");
            usb_host_device_close(client_hdl, dev_hdl);
            dev_hdl = NULL;
            return;
        }

        ESP_LOGI(TAG, "VID: 0x%04X PID: 0x%04X", dev_desc->idVendor, dev_desc->idProduct);

        if (dev_desc->idVendor != 0x045E || dev_desc->idProduct != 0x028E)
        {
            usb_host_device_close(client_hdl, dev_hdl);
            dev_hdl = NULL;
            return;
        }

        const usb_config_desc_t *config_desc = NULL;
        if (usb_host_get_active_config_descriptor(dev_hdl, &config_desc) != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to read config descriptor");
            usb_host_device_close(client_hdl, dev_hdl);
            dev_hdl = NULL;
            return;
        }

        uint8_t ep_in_addr = 0;
        uint16_t ep_in_mps = 0;
        int offset = 0;

        const usb_intf_desc_t *intf = usb_parse_interface_descriptor(config_desc, 0, 0, &offset);
        if (intf == NULL || intf->bInterfaceClass != 0xFF)
        {
            ESP_LOGE(TAG, "Xbox interface not found");
            usb_host_device_close(client_hdl, dev_hdl);
            dev_hdl = NULL;
            return;
        }

        for (int j = 0; j < intf->bNumEndpoints; j++)
        {
            const usb_ep_desc_t *ep = usb_parse_endpoint_descriptor_by_index(intf, j, config_desc->wTotalLength, &offset);
            if (ep != NULL && (ep->bEndpointAddress & 0x80))
            {
                ep_in_addr = ep->bEndpointAddress;
                ep_in_mps = USB_EP_DESC_GET_MPS(ep);
                break;
            }
        }

        if (ep_in_addr == 0)
        {
            ESP_LOGE(TAG, "Failed to find input endpoint");
            usb_host_device_close(client_hdl, dev_hdl);
            dev_hdl = NULL;
            return;
        }

        if (usb_host_interface_claim(client_hdl, dev_hdl, 0, 0) != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to claim interface 0");
            usb_host_device_close(client_hdl, dev_hdl);
            dev_hdl = NULL;
            return;
        }

        if (usb_host_transfer_alloc(ep_in_mps, 0, &in_transfer) != ESP_OK || in_transfer == NULL)
        {
            ESP_LOGE(TAG, "Failed to allocate USB transfer");
            usb_host_interface_release(client_hdl, dev_hdl, 0);
            usb_host_device_close(client_hdl, dev_hdl);
            dev_hdl = NULL;
            return;
        }

        in_transfer->device_handle = dev_hdl;
        in_transfer->bEndpointAddress = ep_in_addr;
        in_transfer->callback = hid_transfer_cb;
        in_transfer->context = NULL;
        in_transfer->timeout_ms = 1000;
        in_transfer->num_bytes = ep_in_mps;

        vTaskDelay(pdMS_TO_TICKS(100));
        if (usb_host_transfer_submit(in_transfer) != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to submit USB transfer");
            usb_host_transfer_free(in_transfer);
            in_transfer = NULL;
            usb_host_interface_release(client_hdl, dev_hdl, 0);
            usb_host_device_close(client_hdl, dev_hdl);
            dev_hdl = NULL;
            return;
        }

        xbox_connected = true;
        memset(&prev_report, 0, sizeof(prev_report));
        latest_lx = 0;
        latest_ly = 0;
        teleplot_send_i32("xbox/connected", 1);
        ESP_LOGI(TAG, "Xbox 360 controller connected and streaming");
    }
    else if (event_msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE)
    {
        if (xbox_connected && event_msg->dev_gone.dev_hdl == dev_hdl)
        {
            ESP_LOGW(TAG, "Xbox controller disconnected");
            xbox_connected = false;
            latest_lx = 0;
            latest_ly = 0;
            teleplot_send_i32("xbox/connected", 0);

            if (in_transfer != NULL)
            {
                usb_host_transfer_free(in_transfer);
                in_transfer = NULL;
            }

            if (dev_hdl != NULL)
            {
                usb_host_interface_release(client_hdl, dev_hdl, 0);
                usb_host_device_close(client_hdl, dev_hdl);
                dev_hdl = NULL;
            }

            memset(&prev_report, 0, sizeof(prev_report));
        }
    }
}

static void usb_lib_task(void *arg)
{
    (void)arg;

    while (1)
    {
        uint32_t event_flags = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)
        {
            ESP_LOGI(TAG, "No USB clients registered");
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE)
        {
            ESP_LOGI(TAG, "All USB devices freed");
        }
    }
}

static void usb_client_task(void *arg)
{
    (void)arg;

    while (1)
    {
        usb_host_client_handle_events(client_hdl, portMAX_DELAY);
    }
}

int joystick_init(void)
{
    if (usb_ready)
    {
        return 0;
    }

    enable_vbus();

    const usb_host_config_t host_config = {
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
        .root_port_unpowered = false,
    };

    if (usb_host_install(&host_config) != ESP_OK)
    {
        ESP_LOGE(TAG, "USB host install failed");
        return -1;
    }

    xTaskCreatePinnedToCore(usb_lib_task, "usb_events", 8192, NULL, 3, NULL, 0);

    const usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 10,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = NULL,
        },
    };

    if (usb_host_client_register(&client_config, &client_hdl) != ESP_OK)
    {
        ESP_LOGE(TAG, "USB client register failed");
        return -1;
    }

    xTaskCreatePinnedToCore(usb_client_task, "usb_client", 8192, NULL, 3, NULL, 0);

    usb_ready = true;
    ESP_LOGI(TAG, "USB joystick support initialized");
    return 0;
}

int joystick_read(motor_control_t *motor_data)
{
    if (motor_data == NULL)
    {
        return -1;
    }

    if (!xbox_connected)
    {
        motor_data->motor1_speed = 0;
        motor_data->motor1_direction = 0;
        motor_data->motor2_speed = 0;
        motor_data->motor2_direction = 0;
        motor_data->timestamp = esp_log_timestamp();
        return -1;
    }

    if (!joystick_tx_enabled)
    {
        motor_data->motor1_speed = 0;
        motor_data->motor1_direction = 0;
        motor_data->motor2_speed = 0;
        motor_data->motor2_direction = 0;
        motor_data->timestamp = esp_log_timestamp();
        return 0;
    }

    motor_data->motor1_speed = latest_left_motor;
    motor_data->motor1_direction = get_direction(latest_left_motor);
    motor_data->motor2_speed = latest_right_motor;
    motor_data->motor2_direction = get_direction(latest_right_motor);
    motor_data->timestamp = esp_log_timestamp();
    return 0;
}

bool joystick_is_connected(void)
{
    return xbox_connected;
}

bool joystick_is_tx_enabled(void)
{
    return joystick_tx_enabled;
}

void joystick_get_raw_values(int16_t *lx, int16_t *ly)
{
    if (lx != NULL)
    {
        *lx = latest_lx;
    }
    if (ly != NULL)
    {
        *ly = latest_ly;
    }
}

void joystick_deinit(void)
{
    if (!usb_ready)
    {
        return;
    }

    if (in_transfer != NULL)
    {
        usb_host_transfer_free(in_transfer);
        in_transfer = NULL;
    }

    if (dev_hdl != NULL)
    {
        usb_host_interface_release(client_hdl, dev_hdl, 0);
        usb_host_device_close(client_hdl, dev_hdl);
        dev_hdl = NULL;
    }

    usb_host_client_deregister(client_hdl);
    usb_host_uninstall();

    usb_ready = false;
    xbox_connected = false;
    joystick_tx_enabled = false;
    latest_lx = 0;
    latest_ly = 0;
    latest_left_motor = 0;
    latest_right_motor = 0;
    memset(&prev_report, 0, sizeof(prev_report));

    ESP_LOGI(TAG, "USB joystick deinitialized");
}
