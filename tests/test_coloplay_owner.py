"""Production MPD transport ownership must survive BLE resets and USB fallback."""
from pathlib import Path
import re,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]


class ColoPlayOwnerTests(unittest.TestCase):
    def test_production_ownership(self):
        source=(ROOT/'firmware/main/mpd_client.c').read_text()
        begin=source.index('static void coloplay_owner(')
        end=source.index('uint8_t mpd_client_coloplay_seek_state(',begin)
        production=source[begin:end]
        fixture=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
static void *mutex=(void*)1;
static void take(void){}
static void give(void){}
static uint32_t coloplay_session,coloplay_ble_session,coloplay_usb_session,coloplay_epoch;
static uint8_t coloplay_seek;
static uint16_t coloplay_volume_completed;
static struct {uint16_t volume_request;} state;
'''+production+r'''
int main(void){
 assert(!mpd_client_coloplay_accepts(0));assert(!strcmp(mpd_client_coloplay_transport(),"none"));
 mpd_client_coloplay_begin(11);assert(!strcmp(mpd_client_coloplay_transport(),"ble"));assert(mpd_client_coloplay_accepts(11));
 const uint32_t before=coloplay_epoch;
 coloplay_seek=1;state.volume_request=coloplay_volume_completed=42;
 mpd_client_coloplay_usb_begin(22);assert(mpd_client_coloplay_accepts(22)&&!mpd_client_coloplay_accepts(11));
 assert(!coloplay_seek&&!state.volume_request&&!coloplay_volume_completed&&coloplay_epoch!=before);
 assert(!strcmp(mpd_client_coloplay_transport(),"usb_cdc_acm"));
 const uint32_t wired=coloplay_epoch;
 mpd_client_coloplay_release();assert(mpd_client_coloplay_accepts(22)&&coloplay_epoch==wired);
 mpd_client_coloplay_begin(33);assert(!strcmp(mpd_client_coloplay_transport(),"usb_cdc_acm"));assert(mpd_client_coloplay_accepts(22)&&coloplay_epoch==wired);
 mpd_client_coloplay_usb_release();assert(!strcmp(mpd_client_coloplay_transport(),"ble"));assert(mpd_client_coloplay_accepts(33)&&!mpd_client_coloplay_accepts(22)&&coloplay_epoch!=wired);
 mpd_client_coloplay_release();assert(!mpd_client_coloplay_accepts(33));assert(!strcmp(mpd_client_coloplay_transport(),"none"));
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp);(path/'owner.c').write_text(fixture)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(path/'owner.c'),'-o',str(path/'owner')],check=True)
            subprocess.run([str(path/'owner')],check=True)


if __name__=='__main__':unittest.main()
