"""Exercise the production Wi-Fi manager with timed events and DHCP/RPC faults."""
import ctypes as C
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class WiFiRecoveryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('cc'):
            raise unittest.SkipTest('C compiler unavailable')
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        work = Path(cls.tmp.name)
        source = (ROOT / 'firmware/main/wifi_manager.c').read_text()
        source = re.sub(r'^#include[^\n]*\n', '', source, flags=re.M)
        boundary = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
typedef int esp_err_t;
typedef int esp_event_base_t;
typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef struct { esp_ip4_addr_t ip; } esp_netif_ip_info_t;
typedef struct { int up; esp_netif_ip_info_t info; } esp_netif_t;
typedef struct { esp_netif_t *esp_netif; esp_netif_ip_info_t ip_info; } ip_event_got_ip_t;
typedef struct { uint8_t reason; } wifi_event_sta_disconnected_t;
typedef struct { int unused; } wifi_ap_record_t;
typedef struct { int unused; } wifi_init_config_t;
typedef struct {
 struct { uint8_t ssid[32],password[64];struct {int authmode;} threshold; } sta;
 struct { uint8_t ssid[32];int ssid_len,channel,max_connection,authmode;struct {bool required;} pmf_cfg; } ap;
} wifi_config_t;
struct { int wifi_mode;char ap_ssid[33]; } g_app_config;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NVS_NO_FREE_PAGES 1
#define ESP_ERR_NVS_NEW_VERSION_FOUND 2
#define ESP_ERR_WIFI_NOT_STARTED 3
#define ESP_ERR_WIFI_NOT_CONNECT 4
#define ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED 5
#define ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED 6
#define WIFI_EVENT 1
#define IP_EVENT 2
#define ESP_EVENT_ANY_ID -1
#define WIFI_EVENT_STA_START 10
#define WIFI_EVENT_STA_STOP 11
#define WIFI_EVENT_STA_CONNECTED 12
#define WIFI_EVENT_STA_DISCONNECTED 13
#define WIFI_EVENT_AP_START 14
#define WIFI_EVENT_AP_STOP 15
#define IP_EVENT_STA_GOT_IP 20
#define IP_EVENT_STA_LOST_IP 21
#define WIFI_MODE_STA 0
#define WIFI_MODE_AP 1
#define WIFI_IF_STA 0
#define WIFI_IF_AP 1
#define WIFI_AUTH_OPEN 0
#define WIFI_AUTH_WPA2_PSK 2
#define BSP_FEATURE_WIFI 1
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(n) (n)
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(p) (unsigned)((p)->addr&255),(unsigned)(((p)->addr>>8)&255),(unsigned)(((p)->addr>>16)&255),(unsigned)((p)->addr>>24)
static char log_text[8192];static size_t log_used;
static void test_log(const char *fmt,...) {
 va_list args;va_start(args,fmt);
 if(log_used < sizeof(log_text)-256) {
  int n=vsnprintf(log_text+log_used,sizeof(log_text)-log_used,fmt,args);
  if(n>0)log_used+=(size_t)n;
  log_text[log_used++]='\n';log_text[log_used]=0;
 }
 va_end(args);
}
#define ESP_LOGI(tag,...) ((void)(tag),test_log(__VA_ARGS__))
#define ESP_LOGW(tag,...) ((void)(tag),test_log(__VA_ARGS__))
#define ESP_LOGE(tag,...) ((void)(tag),test_log(__VA_ARGS__))
static const char *esp_err_to_name(int err) { (void)err;return "injected-error"; }
typedef struct { unsigned capacity,size,count,head;uint8_t items[16][64]; } queue_t;
typedef queue_t *QueueHandle_t;
static queue_t queues[2];static unsigned queue_index;
static QueueHandle_t xQueueCreate(unsigned n,unsigned size) {
 assert(queue_index<2 && n<=16 && size<=64);
 queue_t *q=&queues[queue_index++];memset(q,0,sizeof(*q));q->capacity=n;q->size=size;return q;
}
static int xQueueSend(QueueHandle_t q,const void *p,int wait) {
 assert(wait==0);if(q->count==q->capacity)return 0;
 memcpy(q->items[(q->head+q->count)%q->capacity],p,q->size);q->count++;return 1;
}
static int xQueueReceive(QueueHandle_t q,void *p,int wait) {
 (void)wait;if(!q->count)return 0;
 memcpy(p,q->items[q->head],q->size);q->head=(q->head+1)%q->capacity;q->count--;return 1;
}
static int xQueueOverwrite(QueueHandle_t q,const void *p) {
 assert(q->capacity==1);q->count=q->head=0;return xQueueSend(q,p,0);
}
static int64_t fake_ms;
static int calls_connect,calls_disconnect,calls_start,calls_stop,dhcp_stops,dhcp_starts,delays,ui_calls,power_calls;
static int connect_error,start_error,stop_error,dhcp_stop_error,dhcp_start_error;
static bool associated,lock_available,credentials_available;
static esp_netif_t station,access_point;
static char ui_status[48],web_status[48];
static int registered_ip;
static int64_t esp_timer_get_time(void) { return fake_ms*1000; }
static bool bsp_display_lock(int timeout) { assert(timeout==10);return lock_available; }
static void bsp_display_unlock(void) {}
static void ui_set_network_status(const char *s,const char *ip) { (void)ip;ui_calls++;snprintf(ui_status,sizeof(ui_status),"%s",s); }
static void web_server_set_network_info(const char *s,const char *ip) { (void)ip;snprintf(web_status,sizeof(web_status),"%s",s); }
static int esp_wifi_connect(void) { calls_connect++;return connect_error; }
static int esp_wifi_disconnect(void) { calls_disconnect++;associated=false;station.up=0;station.info.ip.addr=0;return 0; }
static int esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap) { (void)ap;return associated?0:ESP_ERR_WIFI_NOT_CONNECT; }
static int esp_wifi_stop(void) {
 calls_stop++;if(stop_error)return stop_error;
 associated=false;station.up=0;station.info.ip.addr=0;return 0;
}
static int esp_wifi_start(void) { calls_start++;return start_error; }
static int esp_wifi_set_mode(int mode) { (void)mode;return 0; }
static int esp_wifi_set_config(int interface,wifi_config_t *cfg) { (void)interface;(void)cfg;return 0; }
static bool esp_netif_is_netif_up(esp_netif_t *n) { return n->up; }
static int esp_netif_get_ip_info(esp_netif_t *n,esp_netif_ip_info_t *info) { *info=n->info;return 0; }
static int esp_netif_dhcpc_stop(esp_netif_t *n) { (void)n;dhcp_stops++;return dhcp_stop_error; }
static int esp_netif_dhcpc_start(esp_netif_t *n) { (void)n;dhcp_starts++;return dhcp_start_error; }
static int app_config_read_wifi_credentials(char *ssid,size_t n,char *password,size_t p) {
 if(!credentials_available)return -1;
 snprintf(ssid,n,"test-network");snprintf(password,p,"test-secret");return 0;
}
static int bsp_feature_enable(int feature,bool enable) { (void)feature;(void)enable;power_calls++;return 0; }
static void vTaskDelay(int ticks) { (void)ticks;delays++; }
static int nvs_flash_init(void) { return 0; }
static int nvs_flash_erase(void) { return 0; }
static int esp_netif_init(void) { return 0; }
static int esp_event_loop_create_default(void) { return 0; }
static esp_netif_t *esp_netif_create_default_wifi_sta(void) { return &station; }
static esp_netif_t *esp_netif_create_default_wifi_ap(void) { return &access_point; }
static int esp_wifi_init(wifi_init_config_t *cfg) { (void)cfg;return 0; }
static int esp_event_handler_register(int base,int id,void (*handler)(void*,int,int32_t,void*),void *arg) {
 (void)handler;(void)arg;if(base==IP_EVENT)registered_ip=id;return 0;
}
static int xTaskCreate(void (*fn)(void*),const char *name,unsigned stack,void *arg,int prio,void *handle) {
 (void)fn;(void)name;(void)stack;(void)arg;(void)prio;(void)handle;return 1;
}
'''
        exports = r'''
void reset(void) {
 queue_index=0;fake_ms=1000;memset(&station,0,sizeof(station));memset(&access_point,0,sizeof(access_point));
 memset(&g_app_config,0,sizeof(g_app_config));strcpy(g_app_config.ap_ssid,"Tab5");
 calls_connect=calls_disconnect=calls_start=calls_stop=dhcp_stops=dhcp_starts=delays=ui_calls=power_calls=0;
 connect_error=start_error=stop_error=dhcp_stop_error=dhcp_start_error=0;
 associated=false;lock_available=credentials_available=true;log_used=0;log_text[0]=0;
 ui_status[0]=web_status[0]=0;s_current_mode=-1;s_requested_mode=0;s_failures=0;s_deadline_ms=0;
 s_dhcp_restarted=s_status_pending=false;s_station_state=ST_IDLE;
 assert(wifi_manager_start()==0);assert(registered_ip==ESP_EVENT_ANY_ID);delays=0;
}
int mode(int mode) { s_requested_mode=mode;int err=apply_mode(mode,false);if(err)mode_failed(err);return err; }
void emit(int id) {
 if(id==WIFI_EVENT_STA_CONNECTED){associated=true;station.up=1;}
 if(id==WIFI_EVENT_STA_DISCONNECTED){associated=false;station.up=0;station.info.ip.addr=0;}
 wifi_event_sta_disconnected_t data={.reason=201};wifi_event_handler(NULL,WIFI_EVENT,id,&data);
}
void ip_event(int id,int other_interface) {
 ip_event_got_ip_t data={.esp_netif=other_interface?&access_point:&station,.ip_info=station.info};
 wifi_event_handler(NULL,IP_EVENT,id,&data);
}
void set_ip(int has_ip) { station.info.ip.addr=has_ip?0xb500000a:0; }
void set_associated(int up) { associated=up;station.up=up;if(!up)station.info.ip.addr=0; }
void tick(unsigned ms) {
 fake_ms+=ms;manager_event_t event;
 while(xQueueReceive(s_event_queue,&event,0))handle_event(&event);
 check_recovery();
}
void fault(int which,int value) {
 if(which==0)connect_error=value;
 if(which==1)start_error=value;
 if(which==2)stop_error=value;
 if(which==3)dhcp_stop_error=value;
 if(which==4)dhcp_start_error=value;
 if(which==5)lock_available=value;
 if(which==6)credentials_available=value;
}
int counter(int which) {
 int counts[]={calls_connect,calls_disconnect,calls_start,calls_stop,dhcp_stops,dhcp_starts,delays,ui_calls,power_calls};
 return counts[which];
}
int state(void) { return s_station_state; }
int current_mode(void) { return s_current_mode; }
unsigned failures(void) { return s_failures; }
int64_t remaining(void) { return s_deadline_ms-fake_ms; }
const char *status(void) { return web_status; }
const char *ui(void) { return ui_status; }
const char *logs(void) { return log_text; }
int pending_mode(void) { int m=-1;xQueueReceive(s_cmd_queue,&m,0);return m; }
'''
        (work / 'wifi.c').write_text(boundary + source + exports)
        library = work / 'wifi.so'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                        str(work / 'wifi.c'), '-o', str(library)], check=True)
        cls.lib = C.CDLL(str(library))
        cls.lib.remaining.restype = C.c_int64
        for name in ['status', 'ui', 'logs']:
            getattr(cls.lib, name).restype = C.c_char_p

    def setUp(self):
        self.lib.reset()

    def start(self):
        self.assertEqual(self.lib.mode(0), 0)
        self.lib.emit(10)
        self.lib.tick(0)
        self.assertEqual(self.lib.counter(0), 1)
        self.assertEqual(self.lib.status(), b'Connecting...')

    def associate(self):
        self.start()
        self.lib.emit(12)
        self.lib.tick(0)
        self.assertEqual(self.lib.status(), b'Requesting IP...')

    def online(self):
        self.associate()
        self.lib.set_ip(1)
        self.lib.ip_event(20, 0)
        self.lib.tick(0)
        self.assertEqual(self.lib.status(), b'Station connected')

    def test_callbacks_only_enqueue_and_never_connect_or_sleep(self):
        self.lib.mode(0)
        self.lib.emit(10)
        self.assertEqual(self.lib.counter(0), 0)
        self.assertEqual(self.lib.counter(6), 0)
        self.lib.tick(0)
        self.lib.emit(13)
        self.assertEqual(self.lib.counter(0), 1)
        self.assertEqual(self.lib.counter(6), 0)
        self.lib.tick(0)
        self.assertIn(b'reason=201', self.lib.logs())

    def test_normal_connection_never_restarts_dhcp_or_wifi(self):
        self.online()
        self.lib.tick(180000)
        self.assertEqual([self.lib.counter(i) for i in range(6)], [1,0,1,0,0,0])
        self.assertEqual(self.lib.failures(), 0)
        self.assertNotIn(b'test-secret', self.lib.logs())

    def test_dhcp_timeout_restarts_once_then_reconnects(self):
        self.associate()
        self.lib.tick(19999)
        self.assertEqual(self.lib.counter(4), 0)
        self.lib.tick(1)
        self.assertEqual([self.lib.counter(i) for i in [4,5,1]], [1,1,0])
        self.assertEqual(self.lib.status(), b'Retrying DHCP...')
        self.lib.tick(20000)
        self.assertEqual(self.lib.counter(1), 1)
        self.lib.tick(999)
        self.assertEqual(self.lib.counter(0), 1)
        self.lib.tick(1)
        self.assertEqual(self.lib.counter(0), 2)
        self.assertEqual(self.lib.counter(8), 1)  # C6 powered only at startup.

    def test_address_after_dhcp_restart_cancels_recovery(self):
        self.associate()
        self.lib.tick(20000)
        self.lib.set_ip(1)
        self.lib.ip_event(20, 0)
        self.lib.tick(20000)
        self.assertEqual(self.lib.status(), b'Station connected')
        self.assertEqual(self.lib.counter(1), 0)

    def test_connect_rpc_error_retries_without_disconnect_event(self):
        self.lib.fault(0, -1)
        self.lib.mode(0)
        self.lib.emit(10)
        self.lib.tick(0)
        self.assertEqual(self.lib.status(), b'Retrying WiFi...')
        self.assertIn(b'esp_wifi_connect failed', self.lib.logs())
        self.lib.fault(0, 0)
        self.lib.tick(1000)
        self.assertEqual(self.lib.counter(0), 2)

    def test_association_timeout_and_missing_start_event_recover(self):
        self.lib.mode(0)
        self.lib.tick(20000)  # No start event arrives.
        self.assertEqual(self.lib.counter(0), 1)
        self.lib.tick(20000)
        self.assertEqual(self.lib.counter(1), 1)
        self.lib.tick(1000)
        self.assertEqual(self.lib.counter(0), 2)

    def test_missing_connected_event_still_enters_dhcp(self):
        self.start()
        self.lib.set_associated(1)
        self.lib.tick(20000)
        self.assertEqual(self.lib.status(), b'Requesting IP...')
        self.lib.tick(20000)
        self.assertEqual(self.lib.counter(4), 1)

    def test_missing_got_ip_event_is_reconciled(self):
        self.associate()
        self.lib.set_ip(1)
        self.lib.tick(500)
        self.assertEqual(self.lib.status(), b'Station connected')

    def test_lost_ip_without_disconnection_starts_dhcp_recovery(self):
        self.online()
        self.lib.set_ip(0)
        self.lib.ip_event(21, 0)
        self.lib.tick(0)
        self.assertEqual(self.lib.status(), b'Requesting IP...')
        self.lib.tick(20000)
        self.assertEqual(self.lib.counter(4), 1)

    def test_lost_link_without_disconnect_event_recovers(self):
        self.online()
        self.lib.set_associated(0)
        self.lib.tick(500)
        self.assertEqual(self.lib.status(), b'Retrying WiFi...')
        self.lib.tick(1000)
        self.assertEqual(self.lib.counter(0), 2)

    def test_lost_ip_without_event_is_reconciled(self):
        self.online()
        self.lib.set_ip(0)
        self.lib.tick(500)
        self.assertEqual(self.lib.status(), b'Requesting IP...')
        self.lib.tick(20000)
        self.assertEqual(self.lib.counter(4), 1)

    def test_late_success_cancels_retry_even_without_notification(self):
        self.lib.fault(0, -1)
        self.lib.mode(0)
        self.lib.emit(10)
        self.lib.tick(0)
        self.lib.set_associated(1)
        self.lib.set_ip(1)
        self.lib.tick(1000)
        self.assertEqual(self.lib.status(), b'Station connected')
        self.assertEqual(self.lib.counter(0), 1)

    def test_event_queue_overflow_recovers_from_live_netif_state(self):
        self.associate()
        for _ in range(17):
            self.lib.emit(12)
        self.lib.set_ip(1)
        self.lib.ip_event(20, 0)  # Dropped: queue is full.
        self.lib.tick(500)
        self.assertIn(b'event queue full', self.lib.logs())
        self.assertEqual(self.lib.status(), b'Station connected')

    def test_third_failure_restarts_wifi_and_old_stop_event_is_ignored(self):
        self.start()
        for delay in [1000,2000,4000]:
            self.lib.emit(13)
            self.lib.tick(0)
            self.lib.tick(delay)
        self.assertEqual(self.lib.counter(2), 2)
        self.assertEqual(self.lib.counter(3), 1)
        self.assertEqual(self.lib.counter(8), 1)
        self.lib.emit(11)  # Previous stop notification, queued before new start.
        self.lib.emit(13)
        self.lib.emit(10)
        self.lib.tick(0)
        self.assertEqual(self.lib.status(), b'Connecting...')
        self.assertEqual(self.lib.counter(0), 4)

    def test_retry_delay_is_capped_and_success_resets_it(self):
        self.lib.fault(0, -1)
        self.lib.mode(0)
        self.lib.emit(10)
        for _ in range(20):
            self.lib.tick(30000)
            self.lib.emit(10)
            self.lib.tick(0)
            self.assertLessEqual(self.lib.remaining(), 30000)
        self.lib.fault(0, 0)
        self.lib.tick(30000)
        self.lib.emit(10)
        self.lib.tick(0)
        self.lib.set_associated(1)
        self.lib.set_ip(1)
        self.lib.tick(0)
        self.assertEqual(self.lib.failures(), 0)
        self.lib.emit(13)
        self.lib.tick(0)
        self.assertEqual(self.lib.remaining(), 1000)

    def test_dhcp_api_errors_reconnect_instead_of_sticking(self):
        for which in [3,4]:
            with self.subTest(which=which):
                self.lib.reset()
                self.associate()
                self.lib.fault(which, -1)
                self.lib.tick(20000)
                self.assertEqual(self.lib.counter(1), 1)
                self.assertIn(b'DHCP restart failed', self.lib.logs())

    def test_already_stopped_dhcp_can_be_restarted(self):
        self.associate()
        self.lib.fault(3, 5)
        self.lib.tick(20000)
        self.assertEqual(self.lib.counter(5), 1)
        self.assertEqual(self.lib.counter(1), 0)

    def test_boot_mode_start_failure_is_retried(self):
        self.lib.fault(1, -1)
        self.assertNotEqual(self.lib.mode(0), 0)
        self.assertEqual(self.lib.status(), b'WiFi error; retrying...')
        self.lib.fault(1, 0)
        self.lib.tick(1000)
        self.lib.emit(10)
        self.lib.tick(0)
        self.assertEqual(self.lib.counter(0), 1)

    def test_failed_driver_stop_retries_requested_mode(self):
        self.online()
        self.lib.fault(2, -1)
        self.assertNotEqual(self.lib.mode(1), 0)
        self.lib.ip_event(20, 0)  # A late lease must not cancel the mode retry.
        self.lib.tick(0)
        self.lib.fault(2, 0)
        self.lib.tick(1000)
        self.assertEqual(self.lib.current_mode(), 1)
        self.assertEqual(self.lib.counter(0), 1)
        self.assertEqual(self.lib.status(), b'AP started')

    def test_ap_mode_cancels_station_recovery(self):
        self.associate()
        self.lib.tick(20000)
        self.assertEqual(self.lib.mode(1), 0)
        self.lib.emit(14)
        self.lib.emit(13)
        self.lib.tick(120000)
        self.assertEqual(self.lib.status(), b'AP started')
        self.assertEqual(self.lib.current_mode(), 1)
        self.assertEqual(self.lib.counter(2), 2)
        self.assertEqual(self.lib.counter(4), 1)

    def test_no_credentials_falls_back_to_ap_without_restart_loop(self):
        self.lib.fault(6, 0)
        self.assertEqual(self.lib.mode(0), 0)
        self.lib.tick(120000)
        self.assertEqual(self.lib.current_mode(), 1)
        self.assertEqual(self.lib.counter(2), 1)

    def test_ui_lock_contention_retries_status_publication(self):
        self.lib.fault(5, 0)
        self.associate()
        self.assertEqual(self.lib.ui(), b'')
        self.lib.fault(5, 1)
        self.lib.tick(500)
        self.assertEqual(self.lib.ui(), b'Requesting IP...')

    def test_other_interface_got_ip_does_not_mark_wifi_online(self):
        self.associate()
        self.lib.ip_event(20, 1)
        self.lib.tick(0)
        self.assertEqual(self.lib.status(), b'Requesting IP...')

    def test_latest_mode_request_wins(self):
        self.lib.wifi_manager_request_mode(1)
        self.lib.wifi_manager_request_mode(0)
        self.lib.wifi_manager_request_mode(1)
        self.assertEqual(self.lib.pending_mode(), 1)


if __name__ == '__main__':
    unittest.main()
