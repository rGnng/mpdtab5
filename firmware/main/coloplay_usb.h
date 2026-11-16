#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "ui.h"
#include "cJSON.h"
void coloplay_usb_start(void);
void coloplay_usb_publish_navigation(const ui_navigation_t *state);
// MPD worker hands results to the USB task, never performs USB I/O itself.
bool coloplay_usb_result(const uint8_t *command,uint8_t result);
void coloplay_usb_json(cJSON *root);
