#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define USB_NET_DEFAULT_IP "172.32.30.1"
#define USB_NET_DEFAULT_CLIENT_IP "172.32.30.2"
#define USB_NET_IPV4_CAP 16

/* Host-order addresses. USB is a local /24 segment with exactly one peer. */
typedef struct { uint32_t server, client, mask; } usb_net_plan_t;
static inline bool usb_net_parse_ipv4(const char *text, uint32_t *result)
{
    if(!text || !result) return false;
    uint32_t address=0;
    const char *p=text;
    for(unsigned part=0;part<4;part++) {
        unsigned value=0, digits=0;
        while(*p>='0' && *p<='9') {
            value=value*10+(unsigned)(*p++-'0');
            if(++digits>3 || value>255) return false;
        }
        if(!digits) return false;
        address=(address<<8)|value;
        if(part<3) { if(*p++!='.') return false; }
        else if(*p) return false;
    }
    *result=address;
    return true;
}
static inline bool usb_net_address_plan(const char *server, const char *client, usb_net_plan_t *out)
{
    usb_net_plan_t plan={.mask=0xffffff00u};
    if(!usb_net_parse_ipv4(server,&plan.server) || !usb_net_parse_ipv4(client,&plan.client)) return false;
    unsigned first=plan.server>>24, host=plan.server&255, peer=plan.client&255;
    if(!first || first==127 || first>=224 || !host || host==255 || !peer || peer==255 ||
       plan.server==plan.client || (plan.server&plan.mask)!=(plan.client&plan.mask)) return false;
    if(out) *out=plan;
    return true;
}
