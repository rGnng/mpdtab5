"""Run IDF's actual DHCP server with the production USB one-lease setup.

Only lwIP's allocation/timer/UDP transport boundary is replaced. Offers/ACKs
are real DHCP packets built by IDF, including requested gateway/DNS options.
"""
import ctypes as C
import os
import re
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from test_album_view import function
ROOT=Path(__file__).resolve().parents[1]
IDF=Path(os.environ.get('IDF_PATH','/home/aki/esp/esp-idf-5.5.6'))

SHIM=r'''
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <assert.h>
#define LWIP_ASSERT(message,test) assert(test)
#define IPADDR4_INIT(x) { .addr=(x) }
#define DHCP_COARSE_TIMER_MSECS 60000
typedef uint8_t u8_t;typedef uint16_t u16_t;typedef uint32_t u32_t;typedef int16_t s16_t;
typedef int err_t;
#define ERR_OK 0
#define ERR_ARG -1
#define ERR_MEM -2
#define ERR_VAL -3
#define ESP_DHCPS 1
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_DHCP 1
#define LWIP_IANA_HWTYPE_ETHERNET 1
#define ETH_HWADDR_LEN 6
#define PP_HTONL(x) htonl(x)
#define LWIP_MAKEU32(a,b,c,d) (((uint32_t)(a)<<24)|((b)<<16)|((c)<<8)|(d))
#define IPADDR_BROADCAST 0xffffffffu
#define IPADDR_ANY 0
#define IP_IS_V4(x) 1
#define IP4_ADDR(ip,a,b,c,d) ((ip)->addr=htonl(LWIP_MAKEU32(a,b,c,d)))
typedef struct {u32_t addr;} ip4_addr_t;
typedef ip4_addr_t ip_addr_t;
static const ip_addr_t any_address={0};
#define IP_ADDR_ANY (&any_address)
#define ip_2_ip4(x) (x)
#define ip4_addr_set(a,b) (*(a)=*(b))
#define ip4_addr_netcmp(a,b,m) (((a)->addr&(m)->addr)==((b)->addr&(m)->addr))
#define ip4_addr_isany_val(x) (!(x).addr)
#define ip4_addr_isany(x) (!(x)->addr)
#define ip4_addr1(x) ((ntohl((x)->addr)>>24)&255)
#define ip4_addr2(x) ((ntohl((x)->addr)>>16)&255)
#define ip4_addr3(x) ((ntohl((x)->addr)>>8)&255)
#define ip4_addr4(x) (ntohl((x)->addr)&255)
#define mem_calloc calloc
#define mem_free free
#define PBUF_TRANSPORT 0
#define PBUF_RAM 0
struct netif {ip_addr_t ip_addr,netmask,gw;bool up;};
static bool netif_is_up(struct netif *n) {return n->up;}
struct eth_addr {unsigned char addr[6];};
static err_t etharp_add_static_entry(const ip4_addr_t *a,const struct eth_addr *b) {(void)a;(void)b;return 0;}
static err_t etharp_remove_static_entry(const ip4_addr_t *a) {(void)a;return 0;}
struct pbuf {void *payload;u16_t len,tot_len,ref;struct pbuf *next;};
static struct pbuf *pbuf_alloc(int layer,u16_t len,int type) {(void)layer;(void)type;struct pbuf*p=calloc(1,sizeof(*p));p->payload=calloc(1,len);p->len=p->tot_len=len;p->ref=1;return p;}
static u8_t pbuf_free(struct pbuf*p) {if(p){free(p->payload);free(p);}return 1;}
static u16_t pbuf_copy_partial(const struct pbuf*p,void*out,u16_t len,u16_t offset) {if(offset+len>p->len)len=p->len-offset;memcpy(out,(char*)p->payload+offset,len);return len;}
static void sys_timeout(u32_t time,void(*fn)(void*),void*arg) {(void)time;(void)fn;(void)arg;}
static void sys_untimeout(void(*fn)(void*),void*arg) {(void)fn;(void)arg;}
struct udp_pcb {struct netif*netif;void(*receive)(void*,struct udp_pcb*,struct pbuf*,const ip_addr_t*,u16_t);void*arg;};
static struct udp_pcb *udp_new(void) {return calloc(1,sizeof(struct udp_pcb));}
static void udp_remove(struct udp_pcb*p) {free(p);}
static void udp_disconnect(struct udp_pcb*p) {(void)p;}
static err_t udp_bind(struct udp_pcb*p,const ip_addr_t*a,u16_t port) {(void)p;(void)a;(void)port;return 0;}
static void udp_bind_netif(struct udp_pcb*p,const struct netif*n) {p->netif=(struct netif*)n;}
static void udp_recv(struct udp_pcb*p,void(*fn)(void*,struct udp_pcb*,struct pbuf*,const ip_addr_t*,u16_t),void*arg) {p->receive=fn;p->arg=arg;}
static uint8_t output[2048];static unsigned output_len;
static err_t udp_sendto(struct udp_pcb*p,struct pbuf*b,const ip_addr_t*a,u16_t port) {(void)p;(void)a;(void)port;output_len=b->len;memcpy(output,b->payload,b->len);return 0;}
'''

@unittest.skipUnless((IDF/'components/lwip/apps/dhcpserver/dhcpserver.c').exists(),'ESP-IDF DHCP source unavailable')
class UsbDhcpTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work=tempfile.TemporaryDirectory();p=Path(cls.work.name)
        (p/'netshim.h').write_text(SHIM)
        for name in ['dhcp.h','err.h','pbuf.h','udp.h','mem.h','ip_addr.h','timeouts.h','etharp.h','prot/ethernet.h']:
            h=p/'lwip'/name;h.parent.mkdir(parents=True,exist_ok=True);h.write_text('#include "netshim.h"\n')
        (p/'sdkconfig.h').write_text('#define CONFIG_LWIP_DHCPS_MAX_STATION_NUM 8\n#define CONFIG_LWIP_DHCPS_LEASE_UNIT 60\n#define CONFIG_LWIP_DHCPS_STATIC_ENTRIES 1\n')
        source=(ROOT/'firmware/main/usb_network.c').read_text()
        functions='\n'.join(function(source,name) for name in ['dhcp_stop','dhcp_start'])
        driver=r'''
#include "netshim.h"
#include "usb_net_config.h"
#include "dhcpserver/dhcpserver.h"
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_NETIF_REQUESTED_IP_ADDRESS 50
#define ESP_NETIF_SUBNET_MASK 1
#define ESP_NETIF_ROUTER_SOLICITATION_ADDRESS 32
#define ESP_NETIF_DOMAIN_NAME_SERVER 6
typedef int esp_err_t;
static struct netif interface;
static void *s_netif=&interface;
static void *esp_netif_get_netif_impl(void *n) {return n;}
static dhcps_t *s_dhcp;
static void lease_assigned(void *context,uint8_t ip[4],uint8_t mac[6]) {(void)context;(void)ip;(void)mac;}
static bool s_dhcp_running;
static usb_net_plan_t s_plan;
static struct {ip4_addr_t ip;} s_ip_info;
'''
        sanity_source=(IDF/'components/esp_netif/lwip/esp_netif_lwip.c').read_text()
        sanity=function(sanity_source,'esp_netif_config_sanity_check')
        driver_definition=re.search(r'    esp_netif_driver_ifconfig_t driver=\{[^;]+;',source).group()
        driver+=r'''
#define ESP_ERR_INVALID_STATE -2
#define ESP_NETIF_FLAG_IS_BRIDGE 1
#define ESP_LOGE(...) ((void)0)
typedef struct {unsigned flags;void *driver_handle;void *driver_transmit;void *lwip_input_fn;void *lwip_init_fn;} esp_netif_t;
static struct {void *netif;} s_driver;
static esp_err_t transmit_frame(void *handle,void *buffer,size_t length) {(void)handle;(void)buffer;(void)length;return ESP_OK;}
static void free_rx(void *handle,void *buffer) {(void)handle;(void)buffer;}
typedef struct {void *handle;esp_err_t (*transmit)(void*,void*,size_t);void (*driver_free_rx_buffer)(void*,void*);} esp_netif_driver_ifconfig_t;
'''
        driver+=sanity+'\nint startup_driver_valid(void) {\n'+driver_definition+r'''
    esp_netif_t n={.driver_handle=driver.handle,.driver_transmit=(void*)driver.transmit,.lwip_input_fn=(void*)1,.lwip_init_fn=(void*)1};
    return esp_netif_config_sanity_check(&n)==ESP_OK;
}
'''
        suffix=r'''
void setup(void) {
    if(s_dhcp) {dhcp_stop(NULL);dhcps_delete(s_dhcp);}
    usb_net_address_plan(USB_NET_DEFAULT_IP,USB_NET_DEFAULT_CLIENT_IP,&s_plan);
    s_ip_info.ip.addr=htonl(s_plan.server);interface.ip_addr=s_ip_info.ip;interface.netmask.addr=htonl(s_plan.mask);interface.up=true;
    s_dhcp=dhcps_new();if(dhcp_start(NULL))abort();
}
void reconnect(void) {dhcp_stop(NULL);if(dhcp_start(NULL))abort();}
int exchange(const uint8_t *request,unsigned len,uint8_t *response) {
    struct pbuf *p=pbuf_alloc(0,len,0);memcpy(p->payload,request,len);output_len=0;
    ip_addr_t src={0};s_dhcp->dhcps_pcb->receive(s_dhcp,s_dhcp->dhcps_pcb,p,&src,68);
    memcpy(response,output,output_len);return output_len;
}
'''
        dhcp_source=IDF/'components/lwip/apps/dhcpserver/dhcpserver.c'
        (p/'dhcp.c').write_text(driver+'\n#include "'+str(dhcp_source)+'"\n'+functions+suffix)
        subprocess.run(['cc','-shared','-fPIC','-std=gnu11','-Wno-unused-function',
            '-I'+str(p),'-I'+str(ROOT/'firmware/main'),'-I'+str(IDF/'components/lwip/include/apps'),
            str(p/'dhcp.c'),'-o',str(p/'dhcp.so')],check=True)
        cls.lib=C.CDLL(str(p/'dhcp.so'));cls.lib.exchange.argtypes=[C.c_char_p,C.c_uint,C.c_void_p]
    @classmethod
    def tearDownClass(cls):cls.work.cleanup()
    def setUp(self):self.lib.setup()
    def request(self,kind,mac=1,requested=None,ciaddr=None):
        header=bytearray(236);header[0:3]=bytes([1,1,6]);header[4:8]=struct.pack('!I',100+mac)
        header[10:12]=b'\x80\x00';header[28:34]=bytes([2,0,0,0,0,mac])
        if ciaddr:header[12:16]=bytes(map(int,ciaddr.split('.')))
        options=bytes([53,1,kind,55,5,1,3,6,51,54])
        if requested:options+=bytes([50,4])+bytes(map(int,requested.split('.')))+bytes([54,4,172,32,30,1])
        packet=bytes(header)+bytes.fromhex('63825363')+options+b'\xff'
        response=(C.c_uint8*2048)();n=self.lib.exchange(packet,len(packet),response)
        if not n:return None,{}
        data=bytes(response[:n]);opts={};i=240
        while i<len(data) and data[i]!=255:
            key=data[i];i+=1
            if not key:continue
            length=data[i];i+=1;opts[key]=data[i:i+length];i+=length
        return data,opts
    def test_production_driver_passes_idf_startup_sanity_check(self):
        self.assertTrue(self.lib.startup_driver_valid())

    def test_discover_offers_exactly_dot_two_without_gateway_or_dns(self):
        data,options=self.request(1)
        self.assertIsNotNone(data);self.assertEqual(data[16:20],bytes([172,32,30,2]))
        self.assertEqual(options[53],b'\x02');self.assertEqual(options[1],bytes([255,255,255,0]))
        self.assertNotIn(3,options);self.assertNotIn(6,options)
    def test_ack_and_renewal_preserve_single_address_and_omit_gateway(self):
        self.request(1)
        for ciaddr in [None,'172.32.30.2']:
            data,options=self.request(3,requested='172.32.30.2',ciaddr=ciaddr)
            self.assertEqual(options[53],b'\x05');self.assertEqual(data[16:20],bytes([172,32,30,2]))
            self.assertNotIn(3,options);self.assertNotIn(6,options)
    def test_second_client_and_out_of_pool_requests_never_get_another_lease(self):
        self.request(1);self.request(3,requested='172.32.30.2')
        _,options=self.request(1,mac=2);self.assertNotEqual(options.get(53),b'\x02')
        _,options=self.request(3,mac=2,requested='172.32.30.3');self.assertNotEqual(options.get(53),b'\x05')
    def test_reconnect_releases_old_client_for_next_usb_host(self):
        self.request(1);self.request(3,requested='172.32.30.2')
        self.lib.reconnect()
        data,options=self.request(1,mac=2)
        self.assertEqual(options[53],b'\x02');self.assertEqual(data[16:20],bytes([172,32,30,2]))
if __name__=='__main__':unittest.main()
