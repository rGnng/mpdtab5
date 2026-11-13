#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "artwork_quality.h"
#include "esp_err.h"

/* Network transport behind the inherited Tab5 player interface. One worker
 * owns the socket; control calls enqueue work and never wait for MPD. */
typedef struct {
    char host[128];
    uint16_t port;
    char password[128];
} mpd_config_t;

typedef struct {
    bool connected, identified, library_syncing;
    uint8_t play_status; /* 0 stopped, 1 playing, 2 paused */
    char title[128], artist[128], album[128];
    uint64_t track_key, album_pid; /* hashes of URI and album identity */
    char device_name[64];
    uint32_t elapsed_ms, duration_ms;
    int queue_index, queue_count;
    bool shuffle;
    uint8_t repeat; /* 0 off, 1 one, 2 all */
    int volume; /* 0..100, -1 if MPD has no mixer */
    /* Compatibility with the shared device UI: MPD has no battery. */
    int battery;
    bool charging;
    int max_current_ma;
    bool device_will_charge;
    uint8_t accessory_power_mode;
    bool external_charger;
    uint8_t charging_state;
    uint32_t revision;
} mpd_state_t;

/* Init after mounting SD, before player_init; start after lwIP/Wi-Fi setup. */
void mpd_client_init(void);
esp_err_t mpd_client_start(void);
void mpd_client_config(mpd_config_t *out);
esp_err_t mpd_client_configure(const mpd_config_t *config);
void mpd_client_library_id(char *out, size_t cap);
void mpd_client_rescan(void);
bool mpd_client_get_state(mpd_state_t *out);
bool mpd_client_is_enabled(void);
bool mpd_client_is_connected(void);
bool mpd_client_is_identified(void);
bool mpd_client_is_playing(void);
const char *mpd_client_status(void);
esp_err_t mpd_client_refresh_now_playing(void);

/* Legacy HID action names are retained for the common USB/BLE input layer;
 * they generate MPD commands, with no Apple USB playback session. */
esp_err_t mpd_client_hid_command(const char *name);
esp_err_t mpd_client_hid_raw_bit(uint8_t bit);
/* Hold bits: 11 rewind, 10 forward; release stops repeated relative seeking. */
esp_err_t mpd_client_hid_press_bit(uint8_t bit);
esp_err_t mpd_client_hid_release(void);
esp_err_t mpd_client_seek_ms(uint32_t elapsed_ms);
esp_err_t mpd_client_set_shuffle(bool on);

/* Replaces the MPD queue with the selected URI IDs, then starts at index. */
esp_err_t mpd_client_play_track_keys(const uint64_t *keys, int count, int start_index);
esp_err_t mpd_client_play_all_songs(uint64_t start_key);
esp_err_t mpd_client_play_current_key(void);
esp_err_t mpd_client_queue_jump(int index);
int mpd_client_queue_list_count(void);
int mpd_client_queue_list_base(void);
uint32_t mpd_client_queue_list_revision(void);
bool mpd_client_queue_list_get(int index, uint64_t *key);

#define MPD_ARTWORK_SIZE 360
uint32_t mpd_client_artwork_revision(void);
bool mpd_client_artwork_copy(uint8_t *dst, size_t len, artwork_quality_t minimum,
                            artwork_quality_t *actual);
