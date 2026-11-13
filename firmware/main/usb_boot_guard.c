/* Preserve USB attachment panic details across reset, then leave USB-C alone
 * on that build so Wi-Fi/OTA remain available for recovery. */
#include "usb_boot_guard.h"
#include <stdint.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_memory_utils.h"
#include "esp_private/panic_internal.h"
#include "riscv/rvruntime-frames.h"

#define TRACE_MAGIC 0x55534247u
static RTC_NOINIT_ATTR struct {
    uint32_t magic;
    uint8_t elf[32];
    bool armed,blocked;
    uint32_t pc,ra,sp,cause,address;
    uint32_t stack[96];
    char details[256];
} trace;
extern void __real_esp_panic_handler(panic_info_t *info);
extern void __attribute__((noreturn)) __real_panic_abort(const char *details);

void IRAM_ATTR __attribute__((noreturn)) __wrap_panic_abort(const char *details)
{
    if(trace.magic==TRACE_MAGIC && trace.armed) {
        unsigned i=0;
        if(details && esp_ptr_in_dram(details)) {
            for(;i<sizeof(trace.details)-1 && details[i];i++) trace.details[i]=details[i];
        }
        trace.details[i]=0;
    }
    __real_panic_abort(details);
}
void IRAM_ATTR __wrap_esp_panic_handler(panic_info_t *info)
{
    if(trace.magic==TRACE_MAGIC && trace.armed && info) {
        trace.pc=(uint32_t)(uintptr_t)info->addr;
        if(info->frame && esp_ptr_in_dram(info->frame)) {
            const RvExcFrame *f=info->frame;
            trace.pc=f->mepc;trace.ra=f->ra;trace.sp=f->sp;
            trace.cause=f->mcause;trace.address=f->mtval;
            const uint32_t *stack=(const uint32_t *)(uintptr_t)trace.sp;
            for(unsigned i=0;i<96;i++) {
                trace.stack[i]=(!(trace.sp&3) && esp_ptr_in_dram(stack+i)) ? stack[i] : 0;
            }
        }
    }
    __real_esp_panic_handler(info);
}
void usb_boot_guard_init(void)
{
    const uint8_t *elf=esp_app_get_description()->app_elf_sha256;
    if(trace.magic!=TRACE_MAGIC || memcmp(trace.elf,elf,32)) {
        memset(&trace,0,sizeof(trace));trace.magic=TRACE_MAGIC;memcpy(trace.elf,elf,32);
    } else if(trace.armed && (esp_reset_reason()==ESP_RST_PANIC ||
              esp_reset_reason()==ESP_RST_INT_WDT || esp_reset_reason()==ESP_RST_TASK_WDT)) {
        trace.blocked=true;
    }
    trace.armed=false;
    if(trace.blocked) {
        ESP_LOGE("usb_guard","USB disabled after panic: pc=%08lx ra=%08lx cause=%lu address=%08lx %s",
            (unsigned long)trace.pc,(unsigned long)trace.ra,(unsigned long)trace.cause,
            (unsigned long)trace.address,trace.details);
    }
}
bool usb_boot_guard_allow(void) {return !trace.blocked;}
void usb_boot_guard_arm(void) {trace.armed=true;}
void usb_boot_guard_json(cJSON *root)
{
    cJSON *state=cJSON_AddObjectToObject(root,"usb_recovery");
    cJSON_AddBoolToObject(state,"active",trace.blocked);
    if(!trace.blocked) return;
    cJSON_AddNumberToObject(state,"pc",trace.pc);
    cJSON_AddNumberToObject(state,"ra",trace.ra);
    cJSON_AddNumberToObject(state,"sp",trace.sp);
    cJSON_AddNumberToObject(state,"cause",trace.cause);
    cJSON_AddNumberToObject(state,"address",trace.address);
    cJSON_AddStringToObject(state,"details",trace.details);
    cJSON *stack=cJSON_AddArrayToObject(state,"stack");
    extern char _text_start[],_text_end[],_iram_text_start[],_iram_text_end[];
    for(unsigned i=0;i<96;i++) {
        uintptr_t pc=trace.stack[i];
        if((pc>=(uintptr_t)_text_start && pc<(uintptr_t)_text_end) ||
           (pc>=(uintptr_t)_iram_text_start && pc<(uintptr_t)_iram_text_end))
            cJSON_AddItemToArray(stack,cJSON_CreateNumber(pc));
    }
}
