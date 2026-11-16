#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "host/ble_uuid.h"
#include "ui.h"
/* Separate peer driver. D100H service discovery/heartbeat stays in ble_remote. */
void coloplay_ble_reset(void);
void coloplay_ble_service(const ble_uuid_t *uuid,uint16_t start,uint16_t end);
bool coloplay_ble_discover(uint16_t conn,void (*ready)(void),void (*failed)(const char *));
bool coloplay_ble_connected(void);
bool coloplay_ble_notification(uint16_t conn,uint16_t handle,const uint8_t *data,unsigned len);
void coloplay_ble_tick(int64_t now);
/* Called by the MPD worker, never does BLE I/O on that worker. */
void coloplay_ble_result(const uint8_t *command,uint8_t result);
/* UI thread publishes a bounded copy; only the BLE task sends it. */
void coloplay_ble_publish_navigation(const ui_navigation_t *state);
void coloplay_ble_publish_sleep(const cp_sleep_t *state);
