#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "usb/usb_host.h"

static const char *TAG = "USB_OTG_DETECT";
static usb_host_client_handle_t client_hdl;

/**
 * @brief USB Host Daemon Task
 * This task is mandatory to handle USB system events (enumeration, etc.)
 */
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

/**
 * @brief Client Event Callback
 * Triggered when a device is connected or disconnected.
 */
void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    if (event_msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV)
    {
        usb_device_handle_t dev_hdl;
        // Open the device to access its descriptors
        if (usb_host_device_open(client_hdl, event_msg->new_dev.address, &dev_hdl) == ESP_OK)
        {
            const usb_device_desc_t *dev_desc;
            usb_host_get_device_descriptor(dev_hdl, &dev_desc);

            ESP_LOGW(TAG, "--- Device Connected ---");
            ESP_LOGI(TAG, "Vendor ID:  0x%04X", dev_desc->idVendor);
            ESP_LOGI(TAG, "Product ID: 0x%04X", dev_desc->idProduct);
            ESP_LOGW(TAG, "-----------------------");

            usb_host_device_close(client_hdl, dev_hdl);
        }
    }
}

void app_main(void)
{
    // 1. Install USB Host Library
    const usb_host_config_t host_config = {
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));
    ESP_LOGI(TAG, "USB Host Library Installed");

    // 2. Create Daemon Task
    xTaskCreatePinnedToCore(usb_lib_task, "usb_events", 4096, NULL, 2, NULL, 0);

    // 3. Register Client
    const usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = NULL,
        },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &client_hdl));
    xTaskCreatePinnedToCore(usb_client_task, "usb_client", 4096, NULL, 2, NULL, 0);

    ESP_LOGI(TAG, "Waiting for USB device connection on OTG port...");
}