#include "usb_network.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_defaults.h"
#include "esp_netif_net_stack.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_net.h"
#include "lwip/inet.h"
#include "lwip/netif.h"
#include "dhcpserver/dhcpserver.h"
#include "hal/usb_serial_jtag_ll.h"
#include "hal/usb_wrap_ll.h"
#include "soc/usb_wrap_struct.h"

static const char *TAG="usb_net";
static esp_netif_t *s_netif;
static esp_netif_driver_base_t s_driver;
static dhcps_t *s_dhcp;
static bool s_dhcp_running;
static QueueHandle_t s_link_events;
static esp_netif_ip_info_t s_ip_info;
static usb_net_plan_t s_plan;
static char s_ip[16],s_client_ip[16],s_serial[13];
static atomic_bool s_ready,s_connected;
static atomic_uint s_rx,s_tx,s_dropped;
static const char *s_strings[]={
    (const char[]){0x09,0x04}, "rGnng", "MPD Tab5", s_serial,
    "MPD Tab5", "000000000000"
};

void usb_network_status(usb_network_status_t *out)
{
    memset(out,0,sizeof(*out));
    out->ready=atomic_load(&s_ready);
    out->connected=atomic_load(&s_connected);
    if(out->ready) {
        snprintf(out->ip,sizeof(out->ip),"%s",s_ip);
        snprintf(out->client_ip,sizeof(out->client_ip),"%s",s_client_ip);
    }
    out->rx_packets=atomic_load(&s_rx);
    out->tx_packets=atomic_load(&s_tx);
    out->dropped_packets=atomic_load(&s_dropped);
}
static void free_rx(void *handle,void *buffer)
{
    (void)handle; free(buffer);
}
static esp_err_t receive_frame(void *buffer,uint16_t length,void *context)
{
    (void)context;
    if(!atomic_load(&s_connected) || !length || length>1514) return ESP_OK;
    void *copy=malloc(length);
    if(!copy) { atomic_fetch_add(&s_dropped,1); return ESP_ERR_NO_MEM; }
    memcpy(copy,buffer,length);
    atomic_fetch_add(&s_rx,1);
    /* The Ethernet netstack owns/frees the copy on both success and failure. */
    return esp_netif_receive(s_netif,copy,length,NULL);
}
static esp_err_t transmit_frame(void *handle,void *buffer,size_t length)
{
    (void)handle;
    if(!atomic_load(&s_connected)) return ESP_ERR_INVALID_STATE;
    if(!length || length>1514) return ESP_ERR_INVALID_SIZE;
    /* Called on the TCP/IP thread, never on the TinyUSB task. TinyUSB copies
     * the frame before returning; a stalled peer cannot pin lwIP indefinitely. */
    esp_err_t err=tinyusb_net_send_sync(buffer,(uint16_t)length,NULL,pdMS_TO_TICKS(100));
    atomic_fetch_add(err==ESP_OK ? &s_tx : &s_dropped,1);
    return err;
}
static esp_err_t dhcp_stop(void *context)
{
    (void)context;
    if(s_dhcp_running) {
        dhcps_stop(s_dhcp,esp_netif_get_netif_impl(s_netif));
        s_dhcp_running=false;
    }
    return ESP_OK;
}
static void lease_assigned(void *context,uint8_t ip[4],uint8_t mac[6])
{
    (void)context; (void)mac;
    ESP_LOGI(TAG,"USB DHCP client: %u.%u.%u.%u",ip[0],ip[1],ip[2],ip[3]);
}
static esp_err_t dhcp_start(void *context)
{
    (void)context;
    /* esp_netif_dhcps_option() rejects start==end in IDF 5.5. The underlying
     * per-interface DHCP service supports it, so configure it directly in the
     * TCP/IP thread. Never expand the requested one-address pool. */
    dhcps_lease_t pool={.enable=true,
        .start_ip={.addr=htonl(s_plan.client)},.end_ip={.addr=htonl(s_plan.client)}};
    ip4_addr_t mask={.addr=htonl(s_plan.mask)};
    dhcps_offer_t none=0;
    if(dhcps_set_new_lease_cb(s_dhcp,lease_assigned,NULL)!=ERR_OK ||
       dhcps_set_option_info(s_dhcp,ESP_NETIF_REQUESTED_IP_ADDRESS,&pool,sizeof(pool))!=ERR_OK ||
       dhcps_set_option_info(s_dhcp,ESP_NETIF_SUBNET_MASK,&mask,sizeof(mask))!=ERR_OK ||
       dhcps_set_option_info(s_dhcp,ESP_NETIF_ROUTER_SOLICITATION_ADDRESS,&none,sizeof(none))!=ERR_OK ||
       dhcps_set_option_info(s_dhcp,ESP_NETIF_DOMAIN_NAME_SERVER,&none,sizeof(none))!=ERR_OK)
        return ESP_FAIL;
    ip4_addr_t ip={.addr=s_ip_info.ip.addr};
    if(dhcps_start(s_dhcp,esp_netif_get_netif_impl(s_netif),ip)!=ERR_OK) return ESP_FAIL;
    s_dhcp_running=true;
    return ESP_OK;
}
static void link_task(void *arg)
{
    (void)arg;
    bool attached;
    for(;;) {
        if(xQueueReceive(s_link_events,&attached,portMAX_DELAY)!=pdTRUE) continue;
        if(attached) {
            /* Reset leases on every enumeration, even when fast unplug/replug
             * coalesces the detach event in this one-slot queue. */
            if(atomic_exchange(&s_connected,false)) {
                esp_netif_tcpip_exec(dhcp_stop,NULL);
                esp_netif_action_disconnected(s_netif,NULL,0,NULL);
                esp_netif_action_stop(s_netif,NULL,0,NULL);
            }
            esp_netif_action_start(s_netif,NULL,0,NULL);
            esp_netif_action_connected(s_netif,NULL,0,NULL);
            if(esp_netif_tcpip_exec(dhcp_start,NULL)==ESP_OK) {
                atomic_store(&s_connected,true);
                ESP_LOGI(TAG,"USB-C connected: %s, only lease %s, no gateway",s_ip,s_client_ip);
            } else {
                esp_netif_action_disconnected(s_netif,NULL,0,NULL);
                esp_netif_action_stop(s_netif,NULL,0,NULL);
                ESP_LOGE(TAG,"Cannot start USB DHCP service");
            }
        } else if(!attached) {
            atomic_store(&s_connected,false);
            esp_netif_tcpip_exec(dhcp_stop,NULL);
            esp_netif_action_disconnected(s_netif,NULL,0,NULL);
            esp_netif_action_stop(s_netif,NULL,0,NULL);
            ESP_LOGI(TAG,"USB-C disconnected");
        }
    }
}
static void device_event(tinyusb_event_t *event,void *arg)
{
    (void)arg;
    if(event->id==TINYUSB_EVENT_ATTACHED || event->id==TINYUSB_EVENT_DETACHED) {
        bool attached=event->id==TINYUSB_EVENT_ATTACHED;
        xQueueOverwrite(s_link_events,&attached);
    }
}
esp_err_t usb_network_start(const char *ip,const char *client_ip)
{
    if(atomic_load(&s_ready) || s_netif) return ESP_ERR_INVALID_STATE;
    if(!usb_net_address_plan(ip,client_ip,&s_plan)) return ESP_ERR_INVALID_ARG;
    snprintf(s_ip,sizeof(s_ip),"%s",ip);
    snprintf(s_client_ip,sizeof(s_client_ip),"%s",client_ip);
    s_ip_info=(esp_netif_ip_info_t){.ip={.addr=htonl(s_plan.server)},
        .netmask={.addr=htonl(s_plan.mask)},.gw={.addr=0}};
    uint8_t mac[6];
    esp_err_t err=esp_read_mac(mac,ESP_MAC_BASE);
    if(err!=ESP_OK) return err;
    mac[0]=(mac[0]|2u)&0xfeu;
    mac[5]^=0x51;
    snprintf(s_serial,sizeof(s_serial),"%02X%02X%02X%02X%02X%02X",
        mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    esp_netif_inherent_config_t base={
        .flags=0,.ip_info=&s_ip_info,.if_key="USB_NCM",.if_desc="usb",.route_prio=0,
        .get_ip_event=IP_EVENT_ETH_GOT_IP,.lost_ip_event=IP_EVENT_ETH_LOST_IP
    };
    memcpy(base.mac,mac,6);
    esp_netif_driver_ifconfig_t driver={.handle=&s_driver,.transmit=transmit_frame,.driver_free_rx_buffer=free_rx};
    esp_netif_config_t netif_config={.base=&base,.driver=&driver,.stack=ESP_NETIF_NETSTACK_DEFAULT_ETH};
    s_netif=esp_netif_new(&netif_config);
    s_driver.netif=s_netif;
    s_dhcp=dhcps_new();
    s_link_events=xQueueCreate(1,sizeof(bool));
    if(!s_netif || !s_dhcp || !s_link_events) { err=ESP_ERR_NO_MEM; goto fail; }
    esp_netif_set_hostname(s_netif,"mpdtab5-usb");
    if(xTaskCreate(link_task,"usb_net",4096,NULL,5,NULL)!=pdPASS) { err=ESP_ERR_NO_MEM; goto fail; }

    /* The Tab5 USB-C connector uses FSLS PHY0 (GPIO24/25). Moving Serial/JTAG
     * to PHY1 must leave its pads disabled: GPIO26/27 carry the I2S speaker. */
    usb_serial_jtag_ll_phy_enable_pad(false);
    usb_wrap_ll_phy_select(&USB_WRAP,0);
    tinyusb_config_t usb=TINYUSB_DEFAULT_CONFIG(device_event);
    usb.port=TINYUSB_PORT_FULL_SPEED_0;
    usb.task=TINYUSB_TASK_CUSTOM(4096,5,0);
    usb.descriptor.string=s_strings;
    usb.descriptor.string_count=sizeof(s_strings)/sizeof(s_strings[0]);
    err=tinyusb_driver_install(&usb);
    if(err!=ESP_OK) goto fail;
    tinyusb_net_config_t network={.on_recv_callback=receive_frame};
    memcpy(network.mac_addr,mac,6);
    network.mac_addr[5]^=1; /* Host NIC and Tab5 netif must have distinct MACs. */
    err=tinyusb_net_init(&network);
    if(err!=ESP_OK) { tinyusb_driver_uninstall(); goto fail; }
    atomic_store(&s_ready,true);
    ESP_LOGI(TAG,"CDC-NCM ready on USB-C FS: %s/24, DHCP %s only, no gateway",s_ip,s_client_ip);
    return ESP_OK;
fail:
    /* No device-class failure is fatal to Wi-Fi, OTA or the USB-A host. The
     * boot-owned objects stay allocated if their service task was started. */
    ESP_LOGE(TAG,"USB network unavailable: %s",esp_err_to_name(err));
    return err;
}
