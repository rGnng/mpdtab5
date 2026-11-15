#include "wifi_manager.h"

#include <string.h>
#include <stdio.h>

#include "app_config.h"
#include "bsp/esp-bsp.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "ui.h"
#include "web_server.h"

static const char * TAG = "wifi";

/* Only the manager task owns connection state and calls Wi-Fi/DHCP APIs.
 * ESP event callbacks copy events and return without sleeping or making RPCs. */
#define WIFI_CONNECT_TIMEOUT_MS 20000
#define WIFI_DHCP_TIMEOUT_MS 20000
#define WIFI_POLL_MS 500

static QueueHandle_t s_cmd_queue;
static QueueHandle_t s_event_queue;
static esp_netif_t * s_sta_netif;
static esp_netif_t * s_ap_netif;
static int s_current_mode = -1;
static int s_requested_mode;
static unsigned s_failures;
static int64_t s_deadline_ms;
static bool s_dhcp_restarted;
static bool s_status_pending;
static char s_status[48], s_ip[32];

typedef enum {
    ST_IDLE, ST_STARTING, ST_RETRY, ST_CONNECTING, ST_DHCP, ST_ONLINE
} station_state_t;
static station_state_t s_station_state;

typedef struct {
    bool ip_event;
    int32_t id;
    uint8_t reason;
    esp_ip4_addr_t ip;
} manager_event_t;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void publish_ui_status(void)
{
    /* BSP timeout 0 means wait forever. Keep recovery independent of rendering. */
    if(s_status_pending && bsp_display_lock(10)) {
        ui_set_network_status(s_status, s_ip);
        bsp_display_unlock();
        s_status_pending = false;
    }
}

static void post_status(const char * status, const char * ip)
{
    snprintf(s_status, sizeof(s_status), "%s", status);
    snprintf(s_ip, sizeof(s_ip), "%s", ip);
    s_status_pending = true;
    web_server_set_network_info(s_status, s_ip);
    publish_ui_status();
}

static void wifi_event_handler(void * arg, esp_event_base_t base,
                               int32_t id, void * data)
{
    (void)arg;
    manager_event_t event = {.id = id, .ip_event = base == IP_EVENT};
    if(base == WIFI_EVENT) {
        if(id != WIFI_EVENT_STA_START && id != WIFI_EVENT_STA_STOP &&
           id != WIFI_EVENT_STA_CONNECTED && id != WIFI_EVENT_STA_DISCONNECTED &&
           id != WIFI_EVENT_AP_START && id != WIFI_EVENT_AP_STOP) return;
        if(id == WIFI_EVENT_STA_DISCONNECTED && data)
            event.reason = ((wifi_event_sta_disconnected_t *)data)->reason;
    }
    else if(base == IP_EVENT) {
        if(id == IP_EVENT_STA_GOT_IP && data) {
            ip_event_got_ip_t * got = data;
            if(got->esp_netif != s_sta_netif) return;
            event.ip = got->ip_info.ip;
        }
        else if(id != IP_EVENT_STA_LOST_IP) return;
    }
    else return;
    if(s_event_queue && xQueueSend(s_event_queue, &event, 0) != pdTRUE)
        ESP_LOGW(TAG, "event queue full; connection watchdog will reconcile state");
}

static unsigned retry_delay_ms(void)
{
    unsigned shift = s_failures > 5 ? 5 : s_failures ? s_failures - 1 : 0;
    unsigned delay = 1000u << shift;
    return delay > 30000 ? 30000 : delay;
}

static void schedule_retry(const char * reason)
{
    if(s_failures < 1000000) s_failures++;
    s_station_state = ST_RETRY;
    s_dhcp_restarted = false;
    s_deadline_ms = now_ms() + retry_delay_ms();
    ESP_LOGW(TAG, "%s; retry %u in %u ms", reason, s_failures, retry_delay_ms());
    post_status("Retrying WiFi...", "--");
}

static void connect_station(void)
{
    s_station_state = ST_CONNECTING;
    s_dhcp_restarted = false;
    s_deadline_ms = now_ms() + WIFI_CONNECT_TIMEOUT_MS;
    post_status("Connecting...", "--");
    esp_err_t err = esp_wifi_connect();
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        schedule_retry("connection request failed");
    }
}

static void reconnect_station(const char * reason)
{
    esp_err_t err = esp_wifi_disconnect();
    if(err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT)
        ESP_LOGW(TAG, "esp_wifi_disconnect failed: %s", esp_err_to_name(err));
    schedule_retry(reason);
}

static void waiting_for_ip(void)
{
    s_station_state = ST_DHCP;
    s_dhcp_restarted = false;
    s_deadline_ms = now_ms() + WIFI_DHCP_TIMEOUT_MS;
    ESP_LOGI(TAG, "WiFi associated; waiting for DHCP address");
    post_status("Requesting IP...", "--");
}

static void station_online(const esp_ip4_addr_t * address)
{
    char ip[32];
    snprintf(ip, sizeof(ip), IPSTR, IP2STR(address));
    ESP_LOGI(TAG, "station got IP %s (after %u retries)", ip, s_failures);
    s_station_state = ST_ONLINE;
    s_failures = 0;
    s_deadline_ms = 0;
    s_dhcp_restarted = false;
    post_status("Station connected", ip);
}

static esp_err_t apply_mode(int mode, bool force)
{
    if(mode == s_current_mode && !force) return ESP_OK;
    s_station_state = ST_IDLE;
    s_deadline_ms = 0;
    s_dhcp_restarted = false;

    if(s_current_mode != -1) {
        esp_err_t stop_err = esp_wifi_stop();
        if(stop_err != ESP_OK && stop_err != ESP_ERR_WIFI_NOT_STARTED) {
            ESP_LOGE(TAG, "esp_wifi_stop failed: %s", esp_err_to_name(stop_err));
            return stop_err;
        }
        s_current_mode = -1;
    }

    wifi_config_t cfg = {0};
    esp_err_t err;

    if(mode == 1) {
        size_t ap_len = strlen(g_app_config.ap_ssid);
        if(ap_len > sizeof(cfg.ap.ssid)) ap_len = sizeof(cfg.ap.ssid);
        memcpy(cfg.ap.ssid, g_app_config.ap_ssid, ap_len);
        cfg.ap.ssid_len = ap_len;
        cfg.ap.channel = 6;
        cfg.ap.max_connection = 4;
        cfg.ap.authmode = WIFI_AUTH_OPEN;
        cfg.ap.pmf_cfg.required = false;

        err = esp_wifi_set_mode(WIFI_MODE_AP);
        if(err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &cfg);
        if(err == ESP_OK) err = esp_wifi_start();
        if(err == ESP_OK) {
            s_current_mode = 1;
            ESP_LOGI(TAG, "open AP '%s'", g_app_config.ap_ssid);
            post_status("AP started", "192.168.4.1");
        }
        return err;
    }

    char ssid[33] = {0};
    char password[65] = {0};
    err = app_config_read_wifi_credentials(ssid, sizeof(ssid),
                                           password, sizeof(password));
    if(err != ESP_OK || ssid[0] == '\0') {
        ESP_LOGW(TAG, "no usable SD or NVS Wi-Fi credentials; starting open AP fallback");
        return apply_mode(1, false);
    }

    size_t ssid_len = strlen(ssid);
    if(ssid_len > sizeof(cfg.sta.ssid)) ssid_len = sizeof(cfg.sta.ssid);
    memcpy(cfg.sta.ssid, ssid, ssid_len);
    size_t pass_len = strlen(password);
    if(pass_len > sizeof(cfg.sta.password)) pass_len = sizeof(cfg.sta.password);
    memcpy(cfg.sta.password, password, pass_len);
    cfg.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if(err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if(err == ESP_OK) err = esp_wifi_start();
    if(err == ESP_OK) {
        s_current_mode = 0;
        s_station_state = ST_STARTING;
        s_dhcp_restarted = false;
        s_deadline_ms = now_ms() + WIFI_CONNECT_TIMEOUT_MS;
        ESP_LOGI(TAG, "station SSID '%s'", ssid);
        post_status("Connecting...", "--");
    }
    return err;
}

static void mode_failed(esp_err_t err)
{
    ESP_LOGE(TAG, "WiFi mode %d failed: %s", s_requested_mode, esp_err_to_name(err));
    schedule_retry("WiFi setup failed");
    /* Even a failed stop must retry mode setup, not connect in a stopped mode. */
    s_station_state = ST_IDLE;
    post_status("WiFi error; retrying...", "--");
}

static void handle_event(const manager_event_t * event)
{
    if(s_current_mode == 1) {
        if(!event->ip_event && event->id == WIFI_EVENT_AP_START)
            post_status("AP started", "192.168.4.1");
        else if(!event->ip_event && event->id == WIFI_EVENT_AP_STOP)
            post_status("AP stopped", "--");
        return;
    }
    if(s_current_mode != 0) return;
    if(event->ip_event) {
        if(event->id == IP_EVENT_STA_GOT_IP && s_station_state != ST_IDLE) {
            /* Ignore stale IP events left over from an intentional disconnect. */
            esp_netif_ip_info_t info;
            if(esp_netif_is_netif_up(s_sta_netif) &&
               esp_netif_get_ip_info(s_sta_netif, &info) == ESP_OK &&
               info.ip.addr && info.ip.addr == event->ip.addr)
                station_online(&info.ip);
        }
        else if(event->id == IP_EVENT_STA_LOST_IP && s_station_state == ST_ONLINE) {
            ESP_LOGW(TAG, "station lost its IP address");
            waiting_for_ip();
        }
        return;
    }
    if(event->id == WIFI_EVENT_STA_START && s_station_state == ST_STARTING)
        connect_station();
    else if(event->id == WIFI_EVENT_STA_CONNECTED && s_station_state == ST_CONNECTING)
        waiting_for_ip();
    else if(event->id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "station disconnected: reason=%u, state=%d",
                 (unsigned)event->reason, (int)s_station_state);
        if(s_station_state == ST_CONNECTING || s_station_state == ST_DHCP ||
           s_station_state == ST_ONLINE) schedule_retry("WiFi disconnected");
    }
    else if(event->id == WIFI_EVENT_STA_STOP && s_station_state != ST_STARTING &&
            s_station_state != ST_IDLE) {
        s_current_mode = -1;
        mode_failed(ESP_ERR_WIFI_NOT_STARTED);
    }
}

static void check_recovery(void)
{
    publish_ui_status();
    if(s_current_mode == 1 && s_station_state != ST_IDLE) return;
    if(s_station_state == ST_ONLINE || s_station_state == ST_DHCP ||
       s_station_state == ST_CONNECTING || s_station_state == ST_RETRY) {
        esp_netif_ip_info_t info;
        bool up = esp_netif_is_netif_up(s_sta_netif);
        bool got_ip = up && esp_netif_get_ip_info(s_sta_netif, &info) == ESP_OK && info.ip.addr;
        if(got_ip) {
            if(s_station_state != ST_ONLINE) station_online(&info.ip);
            return;
        }
        if(s_station_state == ST_ONLINE) {
            /* Also recover when the event queue dropped a disconnect/lost-IP event. */
            if(up) waiting_for_ip();
            else reconnect_station("station link lost");
            return;
        }
    }
    if(!s_deadline_ms || now_ms() < s_deadline_ms) return;
    if(s_station_state == ST_IDLE) {
        esp_err_t err = apply_mode(s_requested_mode, true);
        if(err != ESP_OK) mode_failed(err);
    }
    else if(s_station_state == ST_RETRY) {
        if(s_failures && s_failures % 3 == 0) {
            ESP_LOGW(TAG, "repeated failures; restarting WiFi driver (no C6/Tab5 reset)");
            esp_err_t err = apply_mode(0, true);
            if(err != ESP_OK) mode_failed(err);
        }
        else connect_station();
    }
    else if(s_station_state == ST_STARTING) {
        /* A missing STA_START notification must not leave the manager stuck. */
        connect_station();
    }
    else if(s_station_state == ST_CONNECTING) {
        wifi_ap_record_t ap;
        if(esp_wifi_sta_get_ap_info(&ap) == ESP_OK) waiting_for_ip();
        else reconnect_station("WiFi association timed out (20 s)");
    }
    else if(s_station_state == ST_DHCP) {
        wifi_ap_record_t ap;
        if(esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
            reconnect_station("WiFi disconnected while waiting for DHCP");
            return;
        }
        if(s_dhcp_restarted) {
            reconnect_station("DHCP still has no address after restart (40 s)");
            return;
        }
        ESP_LOGW(TAG, "DHCP timed out (20 s); restarting DHCP client");
        esp_err_t err = esp_netif_dhcpc_stop(s_sta_netif);
        if(err == ESP_OK || err == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED)
            err = esp_netif_dhcpc_start(s_sta_netif);
        if(err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
            ESP_LOGW(TAG, "DHCP restart failed: %s", esp_err_to_name(err));
            reconnect_station("DHCP restart failed");
            return;
        }
        s_dhcp_restarted = true;
        s_deadline_ms = now_ms() + WIFI_DHCP_TIMEOUT_MS;
        post_status("Retrying DHCP...", "--");
    }
}

static void wifi_manager_task(void * arg)
{
    (void)arg;
    s_requested_mode = g_app_config.wifi_mode == 1 ? 1 : 0;
    esp_err_t err = apply_mode(s_requested_mode, false);
    if(err != ESP_OK) mode_failed(err);

    for(;;) {
        int requested;
        if(xQueueReceive(s_cmd_queue, &requested, 0) == pdTRUE) {
            s_requested_mode = requested;
            if(requested != s_current_mode) {
                ESP_LOGI(TAG, "switch mode -> %d", requested);
                s_failures = 0;
                s_deadline_ms = 0;
                err = apply_mode(requested, false);
                if(err != ESP_OK) mode_failed(err);
            }
        }
        manager_event_t event;
        if(xQueueReceive(s_event_queue, &event, pdMS_TO_TICKS(WIFI_POLL_MS)) == pdTRUE)
            handle_event(&event);
        check_recovery();
    }
}

esp_err_t wifi_manager_start(void)
{
    /* The Tab5 C6 module is powered through an IO expander pin; it is off
     * until BSP_FEATURE_WIFI is enabled. */
    esp_err_t power_err = bsp_feature_enable(BSP_FEATURE_WIFI, true);
    if(power_err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi power enable failed: %s", esp_err_to_name(power_err));
    }
    vTaskDelay(pdMS_TO_TICKS(200));

    esp_err_t err = nvs_flash_init();
    if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if(err != ESP_OK) return err;

    err = esp_netif_init();
    if(err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if(err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if(s_sta_netif == NULL || s_ap_netif == NULL) return ESP_FAIL;

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_cmd_queue = xQueueCreate(1, sizeof(int));
    s_event_queue = xQueueCreate(16, sizeof(manager_event_t));
    if(s_cmd_queue == NULL || s_event_queue == NULL) return ESP_ERR_NO_MEM;

    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                     wifi_event_handler, NULL);
    if(err != ESP_OK) return err;
    err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                     wifi_event_handler, NULL);
    if(err != ESP_OK) return err;

    if(xTaskCreate(wifi_manager_task, "wifi_mgr", 6144, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "manager started (mode=%u)", (unsigned)g_app_config.wifi_mode);
    return ESP_OK;
}

void wifi_manager_request_mode(int mode)
{
    if(s_cmd_queue == NULL) return;
    int m = (mode == 1) ? 1 : 0;
    xQueueOverwrite(s_cmd_queue, &m);
}
