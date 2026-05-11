#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "usb/usb_host.h"

#define TARGET_VID 0x045E
#define TARGET_PID 0x028E

static const char *TAG = "XBOX360_HOST";

// 20 bytes payload for Xbox 360 controller
typedef struct __attribute__((packed)) {
    uint8_t message_type;
    uint8_t packet_size;
    uint8_t buttons1;
    uint8_t buttons2;
    uint8_t lt;
    uint8_t rt;
    int16_t lx;
    int16_t ly;
    int16_t rx;
    int16_t ry;
    uint8_t reserved[6];
} xbox360_report_t;

static usb_host_client_handle_t client_hdl;
static usb_device_handle_t device_hdl;
static usb_transfer_t *xfer_urb;
static bool is_device_connected = false;
static bool has_claimed_interface = false;

// Normalize stick values to roughly [-1.0f, +1.0f]
static float normalize_stick(int16_t val) {
    if (val < 0) {
        return (float)val / 32768.0f;
    } else {
        return (float)val / 32767.0f;
    }
}

static void xfer_in_cb(usb_transfer_t *transfer) {
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED && transfer->actual_num_bytes >= 20) {
        xbox360_report_t *r = (xbox360_report_t *)transfer->data_buffer;
        
        if (r->message_type == 0x00) {
            ESP_LOGI(TAG, "L_X: %+.2f | L_Y: %+.2f | R_X: %+.2f | R_Y: %+.2f | LT: %3d | RT: %3d | BTN1: 0x%02x | BTN2: 0x%02x",
                     normalize_stick(r->lx),
                     normalize_stick(r->ly),
                     normalize_stick(r->rx),
                     normalize_stick(r->ry),
                     r->lt, r->rt, r->buttons1, r->buttons2);
        }
    }
    
    if (is_device_connected) {
        usb_host_transfer_submit(transfer);
    }
}

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg) {
    if (event_msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        if (!is_device_connected) {
            ESP_LOGI(TAG, "Opening device address: %d", event_msg->new_dev.address);
            ESP_ERROR_CHECK(usb_host_device_open(client_hdl, event_msg->new_dev.address, &device_hdl));
            is_device_connected = true;
        }
    } else if (event_msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        if (device_hdl != NULL) {
            ESP_LOGI(TAG, "Device disconnected");
            is_device_connected = false;
            
            if (has_claimed_interface) {
                usb_host_interface_release(client_hdl, device_hdl, 0);
                has_claimed_interface = false;
            }
            if (xfer_urb) {
                usb_host_transfer_free(xfer_urb);
                xfer_urb = NULL;
            }
            usb_host_device_close(client_hdl, device_hdl);
            device_hdl = NULL;
        }
    }
}

static void host_lib_task(void *arg) {
    while (1) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Initializing USB Host for Xbox 360 Controller mapping");
    
    const usb_host_config_t host_config = { 
        .skip_phy_setup = false, 
        .intr_flags = ESP_INTR_FLAG_LEVEL1 
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    xTaskCreate(host_lib_task, "usb_events", 4096, NULL, 5, NULL);

    usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async.client_event_callback = client_event_cb,
        .async.callback_arg = client_hdl,
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &client_hdl));

    while (1) {
        usb_host_client_handle_events(client_hdl, pdMS_TO_TICKS(50));

        if (is_device_connected && device_hdl != NULL && xfer_urb == NULL) {
            const usb_device_desc_t *dev_desc;
            usb_host_get_device_descriptor(device_hdl, &dev_desc);

            if (dev_desc->idVendor == TARGET_VID && dev_desc->idProduct == TARGET_PID) {
                ESP_LOGI(TAG, "Xbox 360 Controller Recognized!");

                const usb_config_desc_t *config_desc;
                usb_host_get_active_config_descriptor(device_hdl, &config_desc);
                
                const usb_ep_desc_t* in_ep = NULL;
                uint8_t interface_number = 0;
                int offset = 0;
                while (offset < config_desc->wTotalLength) {
                    uint8_t len = ((uint8_t*)config_desc)[offset];
                    uint8_t type = ((uint8_t*)config_desc)[offset + 1];
                    if (type == USB_W_VALUE_DT_INTERFACE) {
                        const usb_intf_desc_t *intf = (const usb_intf_desc_t *)&(((uint8_t*)config_desc)[offset]);
                        interface_number = intf->bInterfaceNumber;
                    }
                    if (type == USB_W_VALUE_DT_ENDPOINT) {
                        const usb_ep_desc_t *ep = (const usb_ep_desc_t *)&(((uint8_t*)config_desc)[offset]);
                        if ((ep->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) && 
                            (ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) == USB_BM_ATTRIBUTES_XFER_INT) {
                            in_ep = ep;
                            break;
                        }
                    }
                    offset += len;
                }

                if (in_ep) {
                    ESP_ERROR_CHECK(usb_host_interface_claim(client_hdl, device_hdl, interface_number, 0));
                    has_claimed_interface = true;
                    
                    ESP_ERROR_CHECK(usb_host_transfer_alloc(in_ep->wMaxPacketSize, 0, &xfer_urb));
                    xfer_urb->device_handle = device_hdl;
                    xfer_urb->bEndpointAddress = in_ep->bEndpointAddress;
                    xfer_urb->callback = xfer_in_cb;
                    xfer_urb->context = NULL;
                    xfer_urb->timeout_ms = 0;
                    xfer_urb->num_bytes = in_ep->wMaxPacketSize;
                    
                    ESP_ERROR_CHECK(usb_host_transfer_submit(xfer_urb));
                    ESP_LOGI(TAG, "Polling started on EP %02X", in_ep->bEndpointAddress);
                }
            } else {
                ESP_LOGW(TAG, "Unsupported device VID:%04X PID:%04X", dev_desc->idVendor, dev_desc->idProduct);
                is_device_connected = false;
                usb_host_device_close(client_hdl, device_hdl);
                device_hdl = NULL;
            }
        }
    }
}