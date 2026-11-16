"""Exercise the production BLE key adapter without emitting native power reports."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SleepKeyTest(unittest.TestCase):
    def test_sleep_key_uses_negotiated_screen_off_and_never_legacy_hid(self):
        source = (ROOT / 'coloplay/firmware/src/bluetooth_service.cpp').read_text()
        start = source.index('bool send(uint8_t kind,')
        end = source.index('\nvoid cancel()', start)
        boundary = r'''
#include "remote_feedback.h"
#include <cassert>
static int gMux;
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define pdTRUE 1
static uint32_t now=100,gSession=7,gDropped,gRequestAt;
static uint16_t gNextRequest;
static uint32_t millis(){return now;}
static void *gQueue=(void*)1;
struct Out{uint8_t bytes[20];};
static RemoteFeedback gFeedback;
static RemoteFeedback::Snapshot snapshot(){return gFeedback.snapshot(now);}
static Out captured;static unsigned sent;
static int xQueueSend(void*,const Out *out,int){captured=*out;++sent;return pdTRUE;}
'''
        main = r'''
int main(){
 gFeedback.state.connected=gFeedback.state.secured=true;
 assert(!send(CP_KEY,CP_KEY_SCREEN_OFF,1,0,0)&&!sent);
 uint8_t p[20];cp_header(p,CP_HELLO,0,7);p[17]=CP_FEATURE_SLEEP;
 assert(gFeedback.accept(p,20,7,now));
 assert(!send(CP_KEY,CP_KEY_SCREEN_OFF,1,0,0)&&!sent); // Initial state required.
 cp_sleep_t sleep={CP_SLEEP_ON,30,1800000};cp_sleep_packet(p,7,&sleep);
 assert(gFeedback.accept(p,20,7,now));
 assert(send(CP_KEY,CP_KEY_SCREEN_OFF,9,0,0)&&sent==1);
 assert(cp_command_valid(captured.bytes,20)&&captured.bytes[1]==CP_SLEEP&&cp_u16(captured.bytes+8)==CP_SLEEP_OFF&&cp_u16(captured.bytes+10)==1);
 assert(!send(CP_KEY,CP_KEY_SCREEN_OFF,1,1,0)&&sent==1);
 now+=2000;assert(!send(CP_KEY,CP_KEY_SCREEN_OFF,1,0,0)&&sent==1);
 now=100;gFeedback.state.extension=false;assert(!send(CP_KEY,CP_KEY_SCREEN_OFF,1,0,0)&&sent==1);
 assert(send(CP_KEY,0x52,1,0,0)&&sent==2&&captured.bytes[1]==CP_KEY);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / 'sleep.cpp').write_text(boundary + source[start:end] + main)
            binary = path / 'sleep'
            subprocess.run(['c++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
                            '-I' + str(ROOT / 'coloplay/firmware/include'),
                            str(path / 'sleep.cpp'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
