#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "usb_net_config.h"

typedef struct {
    bool ready, connected;
    char ip[USB_NET_IPV4_CAP], client_ip[USB_NET_IPV4_CAP];
    uint32_t rx_packets, tx_packets, dropped_packets;
} usb_network_status_t;

/* Start only with USB-A HS host selected, after esp_netif/event-loop init.
 * USB-C's FS controller belongs exclusively to CDC-NCM until reboot. */
esp_err_t usb_network_start(const char *ip, const char *client_ip);
void usb_network_status(usb_network_status_t *out);
