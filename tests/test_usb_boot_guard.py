"""A USB attachment panic must leave Wi-Fi/OTA available on the next boot."""
import ctypes as C
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from test_album_view import function
ROOT=Path(__file__).resolve().parents[1]

class UsbBootGuardTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work=tempfile.TemporaryDirectory();p=Path(cls.work.name)
        source=(ROOT/'firmware/main/usb_boot_guard.c').read_text()
        record=re.search(r'static RTC_NOINIT_ATTR struct \{.*?\} trace;',source,re.S).group()
        prefix=r'''
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define RTC_NOINIT_ATTR
#define TRACE_MAGIC 0x55534247u
#define ESP_RST_PANIC 4
#define ESP_RST_INT_WDT 5
#define ESP_RST_TASK_WDT 6
#define ESP_LOGE(...) ((void)0)
static int reason;
static struct {uint8_t app_elf_sha256[32];} app;
static const void *esp_app_get_description_raw(void) {return &app;}
#define esp_app_get_description() (&app)
static int esp_reset_reason(void) {return reason;}
'''
        source=source.replace('void usb_boot_guard_init(', 'static void usb_boot_guard_init(')
        funcs=function(source,'usb_boot_guard_init')
        suffix=r'''
void reset(void) {memset(&trace,0,sizeof(trace));memset(&app,0,sizeof(app));}
int boot(int firmware,int reset_reason) {app.app_elf_sha256[0]=firmware;reason=reset_reason;usb_boot_guard_init();return !trace.blocked;}
void arm(void) {trace.armed=true;}
void capture(void) {trace.pc=0x48012345;}
uint32_t pc(void) {return trace.pc;}
'''
        (p/'guard.c').write_text(prefix+record+funcs+suffix)
        subprocess.run(['cc','-std=c11','-shared','-fPIC',str(p/'guard.c'),'-o',str(p/'guard.so')],check=True)
        cls.lib=C.CDLL(str(p/'guard.so'))
    @classmethod
    def tearDownClass(cls):cls.work.cleanup()
    def setUp(self):self.lib.reset()
    def test_normal_boot_and_deliberate_reboot_allow_usb(self):
        self.assertTrue(self.lib.boot(1,1));self.lib.arm();self.assertTrue(self.lib.boot(1,3))
    def test_usb_panic_disables_usb_on_next_boot_without_losing_crash(self):
        self.lib.boot(1,1);self.lib.arm();self.lib.capture()
        self.assertFalse(self.lib.boot(1,4));self.assertEqual(self.lib.pc(),0x48012345)
        self.assertFalse(self.lib.boot(1,3))
    def test_watchdogs_also_enter_recovery(self):
        for reason in [5,6]:
            self.lib.reset();self.lib.boot(1,1);self.lib.arm();self.assertFalse(self.lib.boot(1,reason))
    def test_panic_before_usb_was_armed_does_not_disable_it(self):
        self.lib.boot(1,1);self.assertTrue(self.lib.boot(1,4))
    def test_new_firmware_clears_previous_build_recovery(self):
        self.lib.boot(1,1);self.lib.arm();self.lib.capture();self.assertFalse(self.lib.boot(1,4))
        self.assertTrue(self.lib.boot(2,3));self.assertEqual(self.lib.pc(),0)
if __name__=='__main__':unittest.main()
