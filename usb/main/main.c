#include <stdio.h>
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

        // For Xbox 360 (045e:028e) we can also print the serial number if needed
        if (dev_desc->idVendor == 0x045E && dev_desc->idProduct == 0x028E)
        {
            ESP_LOGW(TAG, "Xbox 360 Controller detected!");
        }

        // Close device after inspection
        usb_host_device_close(client_hdl, dev_hdl);
        ESP_LOGW(TAG, "-----------------------");
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
    xTaskCreatePinnedToCore(usb_monitor_task, "usb_monitor", 4096, NULL, 2, NULL, 0);

    ESP_LOGW(TAG, "Ready – Plug your Xbox 360 controller into the USB OTG port now");

    // Optional: print initial host status
    usb_host_lib_info_t info;
    if (usb_host_lib_info(&info) == ESP_OK)
    {
        ESP_LOGI(TAG, "Host info: devices=%d, clients=%d", info.num_devices, info.num_clients);
    }
}