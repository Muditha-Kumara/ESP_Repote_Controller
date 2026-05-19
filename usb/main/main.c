#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "usb/usb_host.h"

static const char *TAG = "USB_XBOX";
static usb_host_client_handle_t client_hdl;

// --- Adjust this pin according to your board ---
// ESP32-S3-DevKitC-1: VBUS control on GPIO 10 (active high)
// Some boards use GPIO 14 or no control (always powered)
#define VBUS_GPIO GPIO_NUM_10
// ------------------------------------------------

// Xbox 360 Controller HID Report Format (20 bytes)
// Byte 0: report id, Byte 1: packet size (0x14), then buttons/axes payload
typedef struct
{
    uint8_t report_id;
    uint8_t packet_size;
    uint8_t buttons_low;  // DPAD + START/BACK + LS/RS
    uint8_t buttons_high; // LB/RB/XBOX + A/B/X/Y
    uint8_t lt;
    uint8_t rt;
    int16_t lx;
    int16_t ly;
    int16_t rx;
    int16_t ry;
    uint8_t reserved[6];
} xbox360_hid_report_t;

// Xbox 360 button masks in buttons_low (byte 2)
#define XBOX_DPAD_UP (1 << 0)
#define XBOX_DPAD_DOWN (1 << 1)
#define XBOX_DPAD_LEFT (1 << 2)
#define XBOX_DPAD_RIGHT (1 << 3)
#define XBOX_BTN_START (1 << 4)
#define XBOX_BTN_BACK (1 << 5)
#define XBOX_BTN_LS (1 << 6)
#define XBOX_BTN_RS (1 << 7)

// Xbox 360 button masks in buttons_high (byte 3)
#define XBOX_BTN_LB (1 << 0)
#define XBOX_BTN_RB (1 << 1)
#define XBOX_BTN_XBOX (1 << 2)
#define XBOX_BTN_A (1 << 4)
#define XBOX_BTN_B (1 << 5)
#define XBOX_BTN_X (1 << 6)
#define XBOX_BTN_Y (1 << 7)

// Device context
typedef struct
{
    usb_device_handle_t dev_hdl;
    uint8_t dev_addr;
    usb_transfer_t *in_transfer;
    uint8_t hid_buffer[20];
    xbox360_hid_report_t prev_report;
} xbox360_context_t;

static xbox360_context_t xbox_ctx = {0};
static bool xbox_connected = false;
static SemaphoreHandle_t xbox_ready_sem = NULL;

// Teleplot digital metrics output function
static inline void teleplot_send_i32(const char *series, int32_t value)
{
    printf(">%s:%ld|g\n", series, (long)value);
}

// Teleplot analog metrics output function
static inline void teleplot_send_f32(const char *series, float value)
{
    printf(">%s:%.5f|g\n", series, (double)value);
}

void usb_lib_task(void *arg)
{
    while (1)
    {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)
        {
            ESP_LOGI(TAG, "No clients registered");
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE)
        {
            ESP_LOGI(TAG, "All devices freed");
        }
    }
}

// HID Transfer Callback
static void hid_transfer_cb(usb_transfer_t *transfer)
{
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED)
    {
        // Parse the HID report
        xbox360_hid_report_t *report = (xbox360_hid_report_t *)transfer->data_buffer;

        // Convert byte order for analog sticks (little-endian)
        report->lx = (int16_t)((transfer->data_buffer[7] << 8) | transfer->data_buffer[6]);
        report->ly = (int16_t)((transfer->data_buffer[9] << 8) | transfer->data_buffer[8]);
        report->rx = (int16_t)((transfer->data_buffer[11] << 8) | transfer->data_buffer[10]);
        report->ry = (int16_t)((transfer->data_buffer[13] << 8) | transfer->data_buffer[12]);

        // Emit button states as Teleplot digital channels (0/1).
#define EMIT_BUTTON_FIELD(field, mask, channel)                                   \
    do                                                                            \
    {                                                                             \
        if (((report->field ^ xbox_ctx.prev_report.field) & (mask)) != 0)         \
        {                                                                         \
            teleplot_send_i32("xbox/" channel, (report->field & (mask)) ? 1 : 0); \
        }                                                                         \
    } while (0)

        EMIT_BUTTON_FIELD(buttons_low, XBOX_BTN_BACK, "back");
        EMIT_BUTTON_FIELD(buttons_low, XBOX_BTN_START, "start");
        EMIT_BUTTON_FIELD(buttons_low, XBOX_DPAD_UP, "dpad_up");
        EMIT_BUTTON_FIELD(buttons_low, XBOX_DPAD_DOWN, "dpad_down");
        EMIT_BUTTON_FIELD(buttons_low, XBOX_DPAD_LEFT, "dpad_left");
        EMIT_BUTTON_FIELD(buttons_low, XBOX_DPAD_RIGHT, "dpad_right");
        EMIT_BUTTON_FIELD(buttons_low, XBOX_BTN_LS, "ls");
        EMIT_BUTTON_FIELD(buttons_low, XBOX_BTN_RS, "rs");

        EMIT_BUTTON_FIELD(buttons_high, XBOX_BTN_A, "a");
        EMIT_BUTTON_FIELD(buttons_high, XBOX_BTN_B, "b");
        EMIT_BUTTON_FIELD(buttons_high, XBOX_BTN_X, "x");
        EMIT_BUTTON_FIELD(buttons_high, XBOX_BTN_Y, "y");
        EMIT_BUTTON_FIELD(buttons_high, XBOX_BTN_LB, "lb");
        EMIT_BUTTON_FIELD(buttons_high, XBOX_BTN_RB, "rb");
        EMIT_BUTTON_FIELD(buttons_high, XBOX_BTN_XBOX, "xbox");

#undef EMIT_BUTTON_FIELD

        // Emit analog channels as normalized Teleplot values.
        const int16_t DEAD_ZONE = 3000;
        const int16_t lx_plot = (abs(report->lx) > DEAD_ZONE) ? report->lx : 0;
        const int16_t ly_plot = (abs(report->ly) > DEAD_ZONE) ? report->ly : 0;
        const int16_t rx_plot = (abs(report->rx) > DEAD_ZONE) ? report->rx : 0;
        const int16_t ry_plot = (abs(report->ry) > DEAD_ZONE) ? report->ry : 0;

        const int16_t prev_lx_plot = (abs(xbox_ctx.prev_report.lx) > DEAD_ZONE) ? xbox_ctx.prev_report.lx : 0;
        const int16_t prev_ly_plot = (abs(xbox_ctx.prev_report.ly) > DEAD_ZONE) ? xbox_ctx.prev_report.ly : 0;
        const int16_t prev_rx_plot = (abs(xbox_ctx.prev_report.rx) > DEAD_ZONE) ? xbox_ctx.prev_report.rx : 0;
        const int16_t prev_ry_plot = (abs(xbox_ctx.prev_report.ry) > DEAD_ZONE) ? xbox_ctx.prev_report.ry : 0;

        if (lx_plot != prev_lx_plot)
            teleplot_send_f32("xbox/lx", (float)lx_plot / 32767.0f);
        if (ly_plot != prev_ly_plot)
            teleplot_send_f32("xbox/ly", (float)ly_plot / 32767.0f);
        if (rx_plot != prev_rx_plot)
            teleplot_send_f32("xbox/rx", (float)rx_plot / 32767.0f);
        if (ry_plot != prev_ry_plot)
            teleplot_send_f32("xbox/ry", (float)ry_plot / 32767.0f);

        if (report->lt != xbox_ctx.prev_report.lt)
            teleplot_send_f32("xbox/lt", (float)report->lt / 255.0f);
        if (report->rt != xbox_ctx.prev_report.rt)
            teleplot_send_f32("xbox/rt", (float)report->rt / 255.0f);

        // Update previous report
        memcpy(&xbox_ctx.prev_report, report, sizeof(xbox360_hid_report_t));
    }
    else if (transfer->status != USB_TRANSFER_STATUS_NO_DEVICE)
    {
        ESP_LOGW(TAG, "HID transfer error: %d", transfer->status);
    }

    // Resubmit transfer if device still connected
    if (xbox_connected && xbox_ctx.dev_hdl)
    {
        usb_host_transfer_submit(xbox_ctx.in_transfer);
    }
}

void usb_client_task(void *arg)
{
    while (1)
    {
        usb_host_client_handle_events(client_hdl, portMAX_DELAY);
    }
}

void usb_monitor_task(void *arg)
{
    usb_host_lib_info_t info;
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (usb_host_lib_info(&info) == ESP_OK)
        {
            ESP_LOGI(TAG, "Monitor: devices=%d, clients=%d, suspended=%d",
                     info.num_devices, info.num_clients, info.root_port_suspended);
        }
    }
}

void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    if (event_msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV)
    {
        usb_device_handle_t dev_hdl;
        ESP_LOGI(TAG, "New device detected at address %d", event_msg->new_dev.address);

        // Open device
        esp_err_t err = usb_host_device_open(client_hdl, event_msg->new_dev.address, &dev_hdl);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to open device: %s", esp_err_to_name(err));
            return;
        }

        // Get device descriptor
        const usb_device_desc_t *dev_desc;
        err = usb_host_get_device_descriptor(dev_hdl, &dev_desc);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to get device descriptor: %s", esp_err_to_name(err));
            usb_host_device_close(client_hdl, dev_hdl);
            return;
        }

        ESP_LOGW(TAG, "--- Device Connected ---");
        ESP_LOGI(TAG, "VID: 0x%04X, PID: 0x%04X", dev_desc->idVendor, dev_desc->idProduct);
        ESP_LOGI(TAG, "Class: 0x%02X, Subclass: 0x%02X, Protocol: 0x%02X",
                 dev_desc->bDeviceClass, dev_desc->bDeviceSubClass, dev_desc->bDeviceProtocol);

        // Check if Xbox 360 Controller
        if (dev_desc->idVendor == 0x045E && dev_desc->idProduct == 0x028E)
        {
            ESP_LOGW(TAG, "Xbox 360 Controller detected!");

            // Get configuration descriptor
            const usb_config_desc_t *config_desc;
            err = usb_host_get_active_config_descriptor(dev_hdl, &config_desc);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to get config descriptor: %s", esp_err_to_name(err));
                usb_host_device_close(client_hdl, dev_hdl);
                return;
            }

            ESP_LOGI(TAG, "Config: bNumInterfaces=%d, wTotalLength=%d",
                     config_desc->bNumInterfaces, config_desc->wTotalLength);

            // Find HID IN endpoint (0x81 - Interface 0, IN endpoint)
            uint8_t ep_in_addr = 0;
            uint16_t ep_in_mps = 0;
            int offset = 0;

            // Parse Interface 0
            const usb_intf_desc_t *intf = usb_parse_interface_descriptor(
                config_desc, 0, 0, &offset);

            if (intf == NULL)
            {
                ESP_LOGE(TAG, "Failed to find interface 0");
                usb_host_device_close(client_hdl, dev_hdl);
                return;
            }

            ESP_LOGI(TAG, "Interface 0: Class=0x%02X, Subclass=0x%02X, NumEP=%d",
                     intf->bInterfaceClass, intf->bInterfaceSubClass, intf->bNumEndpoints);

            // Look for HID interface (bInterfaceClass = 0xFF for Xbox 360)
            if (intf->bInterfaceClass == 0xFF)
            {
                for (int j = 0; j < intf->bNumEndpoints; j++)
                {
                    const usb_ep_desc_t *ep = usb_parse_endpoint_descriptor_by_index(
                        intf, j, config_desc->wTotalLength, &offset);

                    if (ep != NULL)
                    {
                        ESP_LOGI(TAG, "  EP %d: addr=0x%02X, attr=0x%02X, wMaxPacketSize=%d",
                                 j, ep->bEndpointAddress, ep->bmAttributes, USB_EP_DESC_GET_MPS(ep));

                        // Check if IN endpoint (bEndpointAddress bit 7 = 1 for IN)
                        if (ep->bEndpointAddress & 0x80)
                        {
                            ep_in_addr = ep->bEndpointAddress;
                            ep_in_mps = USB_EP_DESC_GET_MPS(ep);
                            ESP_LOGI(TAG, "Selected IN endpoint: 0x%02X, MPS: %d", ep_in_addr, ep_in_mps);
                            break;
                        }
                    }
                }
            }

            if (!ep_in_addr)
            {
                ESP_LOGE(TAG, "Failed to find HID IN endpoint");
                usb_host_device_close(client_hdl, dev_hdl);
                return;
            }

            // Claim the interface to access its endpoints
            ESP_LOGI(TAG, "Claiming interface 0...");
            err = usb_host_interface_claim(client_hdl, dev_hdl, 0, 0);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to claim interface: %s", esp_err_to_name(err));
                usb_host_device_close(client_hdl, dev_hdl);
                return;
            }

            ESP_LOGI(TAG, "Interface claimed successfully");

            // Allocate USB transfer (allocate MPS size to match endpoint)
            usb_transfer_t *transfer = NULL;
            err = usb_host_transfer_alloc(ep_in_mps, 0, &transfer);
            if (err != ESP_OK || transfer == NULL)
            {
                ESP_LOGE(TAG, "Failed to allocate transfer: %s", esp_err_to_name(err));
                usb_host_interface_release(client_hdl, dev_hdl, 0);
                usb_host_device_close(client_hdl, dev_hdl);
                return;
            }

            ESP_LOGI(TAG, "Transfer allocated: data_buffer=%p, buffer_size=%d",
                     transfer->data_buffer, transfer->data_buffer_size);

            // Setup transfer
            transfer->device_handle = dev_hdl;
            transfer->bEndpointAddress = ep_in_addr;
            transfer->callback = hid_transfer_cb;
            transfer->context = NULL;
            transfer->timeout_ms = 1000;     // 1 second timeout
            transfer->num_bytes = ep_in_mps; // Read full MPS size

            // Store device context
            xbox_ctx.dev_hdl = dev_hdl;
            xbox_ctx.dev_addr = event_msg->new_dev.address;
            xbox_ctx.in_transfer = transfer;
            xbox_connected = true;

            // Small delay to ensure device is ready
            vTaskDelay(pdMS_TO_TICKS(100));

            // Submit first transfer
            ESP_LOGI(TAG, "Submitting transfer for endpoint 0x%02X...", ep_in_addr);
            err = usb_host_transfer_submit(transfer);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to submit transfer: %s", esp_err_to_name(err));
                usb_host_transfer_free(transfer);
                usb_host_interface_release(client_hdl, dev_hdl, 0);
                usb_host_device_close(client_hdl, dev_hdl);
                xbox_connected = false;
                return;
            }

            ESP_LOGI(TAG, "Xbox 360 HID reading started successfully");
            teleplot_send_i32("xbox/connected", 1);
            xSemaphoreGive(xbox_ready_sem);
        }
        else
        {
            // Not Xbox 360, close device
            usb_host_device_close(client_hdl, dev_hdl);
        }

        ESP_LOGW(TAG, "-----------------------");
    }
    else if (event_msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE)
    {
        ESP_LOGW(TAG, "Device disconnected");

        if (xbox_connected)
        {
            xbox_connected = false;

            if (xbox_ctx.in_transfer)
            {
                usb_host_transfer_free(xbox_ctx.in_transfer);
                xbox_ctx.in_transfer = NULL;
            }

            // Release the interface before closing device
            if (xbox_ctx.dev_hdl)
            {
                usb_host_interface_release(client_hdl, xbox_ctx.dev_hdl, 0);
                usb_host_device_close(client_hdl, xbox_ctx.dev_hdl);
                xbox_ctx.dev_hdl = NULL;
            }

            memset(&xbox_ctx, 0, sizeof(xbox360_context_t));
            memset(&xbox_ctx.prev_report, 0, sizeof(xbox360_hid_report_t));

            ESP_LOGI(TAG, "Xbox 360 controller cleaned up");
            teleplot_send_i32("xbox/connected", 0);
        }
    }
}

static void enable_vbus(void)
{
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << VBUS_GPIO),
        .pull_down_en = 0,
        .pull_up_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(VBUS_GPIO, 1); // Enable 5V to USB port
    ESP_LOGI(TAG, "VBUS enabled on GPIO %d", VBUS_GPIO);
    vTaskDelay(pdMS_TO_TICKS(100)); // Allow power to settle
}

void app_main(void)
{
    // Initialize semaphore for Xbox ready event
    xbox_ready_sem = xSemaphoreCreateBinary();
    if (xbox_ready_sem == NULL)
    {
        ESP_LOGE(TAG, "Failed to create semaphore");
        return;
    }

    // 1. Enable 5V VBUS (critical for most USB OTG ports)
    enable_vbus();

    // 2. Install USB Host Library
    const usb_host_config_t host_config = {
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
        .root_port_unpowered = false, // We already powered VBUS
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));
    ESP_LOGI(TAG, "USB Host Library installed");

    // 3. Create USB event handling task
    xTaskCreatePinnedToCore(usb_lib_task, "usb_events", 8192, NULL, 3, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(100));

    // 4. Register a client
    const usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 10,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = NULL,
        },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &client_hdl));
    xTaskCreatePinnedToCore(usb_client_task, "usb_client", 8192, NULL, 3, NULL, 0);

    // 5. Start monitor task
    // xTaskCreatePinnedToCore(usb_monitor_task, "usb_monitor", 4096, NULL, 2, NULL, 0);

    ESP_LOGW(TAG, "Ready – Plug your Xbox 360 controller into the USB OTG port now");

    // Optional: print initial host status
    usb_host_lib_info_t info;
    if (usb_host_lib_info(&info) == ESP_OK)
    {
        ESP_LOGI(TAG, "Host info: devices=%d, clients=%d", info.num_devices, info.num_clients);
    }
}