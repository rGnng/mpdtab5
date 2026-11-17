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
    bool shuffle, starred;
    uint8_t repeat; /* 0 off, 1 one, 2 all */
    float volume; /* normalized 0..1, -1 if MPD has no mixer */
    float volume_grid_target; /* confirmed logical target, -1 when unanchored */
    uint8_t volume_grid_steps;
    uint16_t volume_request; /* last completed ColoPlay volume command */
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
/* Atomic mixer/grid-reference snapshot for the ColoPlay feedback service. */
bool mpd_client_get_remote_state(mpd_state_t *out);
esp_err_t mpd_client_coloplay_command(const uint8_t *command);
void mpd_client_coloplay_release(void);
void mpd_client_coloplay_begin(uint32_t session);
void mpd_client_coloplay_usb_begin(uint32_t session);
void mpd_client_coloplay_usb_release(void);
bool mpd_client_coloplay_accepts(uint32_t session);
// Active input owner; USB wins while its validated session is present.
const char *mpd_client_coloplay_transport(void);
uint8_t mpd_client_coloplay_seek_state(void);
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
esp_err_t mpd_client_toggle_star(uint64_t key);
esp_err_t mpd_client_clear_stars(void);
int mpd_client_star_count(void);
bool mpd_client_star_get(int index, uint64_t *key);
int mpd_client_star_copy(uint64_t *keys, int capacity);
uint32_t mpd_client_star_revision(void);

/* Replace the MPD queue, optionally shuffle it, then start playback. Shuffle
 * starts at the first entry of the shuffled queue rather than start_index. */
esp_err_t mpd_client_play_track_keys(const uint64_t *keys, int count, int start_index, bool shuffle);
esp_err_t mpd_client_play_all_songs(uint64_t start_key);
/* Server-side whole-library shuffle; no per-track request allocation. */
esp_err_t mpd_client_shuffle_all(void);
esp_err_t mpd_client_play_current_key(void);
esp_err_t mpd_client_queue_jump(int index);
int mpd_client_queue_list_count(void);
int mpd_client_queue_list_base(void);
uint32_t mpd_client_queue_list_revision(void);
bool mpd_client_queue_list_get(int index, uint64_t *key);
/* A cache miss schedules one bounded 64-entry page; getters never do I/O. */
bool mpd_client_queue_loading(int index);
void mpd_client_queue_stats(int *total,int *cached,int *pending);
/* Stable entry handles include a connection epoch; raw MPD ID zero is valid. */
uint64_t mpd_client_queue_entry(int index);
esp_err_t mpd_client_queue_add(const uint64_t *keys, int count, bool next);
esp_err_t mpd_client_queue_edit(uint64_t entry, bool remove);
bool mpd_client_track_starred(uint64_t key);
/* Full names are copied from the worker's latest playlist list, not UI labels. */
int mpd_client_playlist_destination_count(void);
bool mpd_client_playlist_destination(int index,char *name,size_t size,uint32_t *generation);
esp_err_t mpd_client_playlist_add(const char *name,uint32_t generation,uint64_t key);
/* Copies the visible stored-list snapshot (up to 1024 keys); position is zero-based. */
esp_err_t mpd_client_playlist_remove(const char *name,uint32_t generation,const uint64_t *keys,int count,int position);

#define MPD_ARTWORK_SIZE 360
uint32_t mpd_client_artwork_revision(void);
bool mpd_client_artwork_copy(uint8_t *dst, size_t len, artwork_quality_t minimum,
                            artwork_quality_t *actual);

/* UI reports activity and the settled viewport; calls never perform I/O. */
void mpd_client_artwork_pause(void);
void mpd_client_artwork_view(const uint64_t *albums, unsigned count, bool active);

/* Snapshot of the current idle cache pass; cached image count is supplied by
 * the persistent thumbnail cache, which also includes warm-boot entries. */
typedef struct {
    unsigned total, checked, missing, failed;
    bool active;
    const char *status;
} mpd_artwork_status_t;
void mpd_client_artwork_status(mpd_artwork_status_t *out);
