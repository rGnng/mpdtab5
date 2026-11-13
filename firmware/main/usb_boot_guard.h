#pragma once
#include <stdbool.h>
#include "cJSON.h"
void usb_boot_guard_init(void);
bool usb_boot_guard_allow(void);
void usb_boot_guard_arm(void);
void usb_boot_guard_json(cJSON *root);
