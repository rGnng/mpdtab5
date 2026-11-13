/* Serial and web debug input share the same UI actions as touch and BLE. */
#include "serial_input.h"
#include "ui.h"
#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
static bool map_serial_key(int c, ui_key_t * key)
{
    switch(c) {
        case 'w': case 'W': *key = UI_KEY_UP; return true;
        case 's': case 'S': *key = UI_KEY_DOWN; return true;
        case '\r': case '\n': *key = UI_KEY_ENTER; return true;
        case 'b': case 'B': *key = UI_KEY_BACK; return true;
        case 'p': case 'P': *key = UI_KEY_PAGE_UP; return true;
        case 'n': case 'N': *key = UI_KEY_PAGE_DOWN; return true;
        case '[': *key = UI_KEY_SCREEN_PREV; return true;
        case ']': *key = UI_KEY_SCREEN_NEXT; return true;
        case 'c': case 'C': *key = UI_KEY_CONTEXT; return true;
        case '1': *key = UI_KEY_1; return true;
        case '2': *key = UI_KEY_2; return true;
        case '3': *key = UI_KEY_3; return true;
        case '4': *key = UI_KEY_4; return true;
        case '5': *key = UI_KEY_5; return true;
        case '6': *key = UI_KEY_6; return true;
        case '7': *key = UI_KEY_7; return true;
        default: return false;
    }
}

bool serial_input_inject_key(char c)
{
    ui_key_t key;
    if(!map_serial_key(c, &key)) return false;
    if(bsp_display_lock(0)) {
        ui_handle_key(key);
        bsp_display_unlock();
    }
    return true;
}




static void serial_input_task(void *arg)
{
    (void)arg;
    for(;;) {
        int c=fgetc(stdin);
        if(c==EOF) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        serial_input_inject_key((char)c);
    }
}
void serial_input_start(void)
{
    xTaskCreateWithCaps(serial_input_task,"serial_keys",8192,NULL,5,NULL,
                        MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
}
