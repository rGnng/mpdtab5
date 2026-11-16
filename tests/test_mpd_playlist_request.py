"""Production playlist request ownership and rejection without socket I/O."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def production(source, name):
    match = re.search(r'^(?:static )?[^\n]+\b' + name + r'\([^;]*?\)\s*\{', source, re.M)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


class PlaylistRequestTest(unittest.TestCase):
    def test_queued_data_is_owned_and_all_failure_paths_free_it(self):
        source = (ROOT / 'firmware/main/mpd_client.c').read_text()
        start = source.index('typedef enum { CMD_TEXT')
        types = source[start:source.index('} command_t;', start) + len('} command_t;')]
        prefix = r'''
#include "mpd_wire.h"
#include "coloplay_ble_protocol.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
typedef int esp_err_t;
enum { ESP_OK,ESP_ERR_INVALID_ARG,ESP_ERR_NO_MEM,ESP_ERR_INVALID_STATE };
#define pdPASS 1
static unsigned live,allocations,fail_at;
static void *allocate(size_t n){++allocations;if(fail_at==allocations)return NULL;void *p=malloc(n);assert(p);++live;return p;}
static char *duplicate(const char *s){char *p=allocate(strlen(s)+1);if(p)strcpy(p,s);return p;}
static void release(void *p){if(p){assert(live);--live;free(p);}}
#define malloc allocate
#define strdup duplicate
#define free release
static unsigned config_revision=7;
static struct {int connected;} state={1};
static void *commands;
static int accept=1;
static void take(void){}
static void give(void){}
static void message(const char *text){assert(text);}
'''
        boundary = r'''
static command_t captured;
static int xQueueSend(void *queue,const void *value,int wait){(void)queue;(void)wait;if(!accept)return 0;captured=*(const command_t*)value;return pdPASS;}
'''
        main = r'''
static void reset(void){assert(!live);allocations=fail_at=0;state.connected=accept=1;memset(&captured,0,sizeof(captured));}
int main(void){
 char name[256]="Evening mix";reset();
 assert(mpd_client_playlist_add(name,7,123)==ESP_OK&&live==2);
 assert(captured.kind==CMD_PLAYLIST_ADD&&captured.generation==7&&captured.playlist_generation==7&&captured.count==1&&captured.keys[0]==123);
 strcpy(name,"Changed caller buffer");assert(!strcmp(captured.playlist,"Evening mix"));release(captured.keys);release(captured.playlist);
 for(unsigned failed=1;failed<=2;failed++){reset();fail_at=failed;assert(mpd_client_playlist_add("Mix",7,123)==ESP_ERR_NO_MEM&&!live);}
 reset();accept=0;assert(mpd_client_playlist_add("Mix",7,123)==ESP_ERR_NO_MEM&&!live);
 reset();state.connected=0;assert(mpd_client_playlist_add("Mix",7,123)==ESP_ERR_INVALID_STATE&&!live);
 reset();for(const char **p=(const char*[]){"","Stared","sTaReD","Bad\nclear",NULL};*p;p++)assert(mpd_client_playlist_add(*p,7,123)==ESP_ERR_INVALID_ARG);
 assert(mpd_client_playlist_add("Mix",0,123)==ESP_ERR_INVALID_ARG&&mpd_client_playlist_add("Mix",7,0)==ESP_ERR_INVALID_ARG&&!live&&!allocations);
 reset();memset(name,'n',255);name[255]=0;assert(mpd_client_playlist_add(name,7,123)==ESP_OK&&strlen(captured.playlist)==255);release(captured.keys);release(captured.playlist);
 reset();uint64_t keys[1024]={123,456,123};
 assert(mpd_client_playlist_remove("Mix",7,keys,3,2)==ESP_OK&&live==2);
 assert(captured.kind==CMD_PLAYLIST_REMOVE&&captured.generation==7&&captured.playlist_generation==7&&captured.count==3&&captured.start==2);
 keys[2]=999;assert(captured.keys[2]==123);release(captured.keys);release(captured.playlist);
 for(unsigned failed=1;failed<=2;failed++){reset();fail_at=failed;assert(mpd_client_playlist_remove("Mix",7,keys,3,2)==ESP_ERR_NO_MEM&&!live);}
 reset();accept=0;assert(mpd_client_playlist_remove("Mix",7,keys,3,2)==ESP_ERR_NO_MEM&&!live);
 reset();state.connected=0;assert(mpd_client_playlist_remove("Mix",7,keys,3,2)==ESP_ERR_INVALID_STATE&&!live);
 reset();for(const char **p=(const char*[]){"","Stared","sTaReD","Bad\nclear",NULL};*p;p++)assert(mpd_client_playlist_remove(*p,7,keys,3,2)==ESP_ERR_INVALID_ARG);
 assert(mpd_client_playlist_remove("Mix",0,keys,3,2)==ESP_ERR_INVALID_ARG);
 assert(mpd_client_playlist_remove("Mix",7,NULL,3,2)==ESP_ERR_INVALID_ARG);
 assert(mpd_client_playlist_remove("Mix",7,keys,0,0)==ESP_ERR_INVALID_ARG);
 assert(mpd_client_playlist_remove("Mix",7,keys,1025,0)==ESP_ERR_INVALID_ARG);
 assert(mpd_client_playlist_remove("Mix",7,keys,3,-1)==ESP_ERR_INVALID_ARG);
 assert(mpd_client_playlist_remove("Mix",7,keys,3,3)==ESP_ERR_INVALID_ARG);
 keys[0]=0;assert(mpd_client_playlist_remove("Mix",7,keys,3,2)==ESP_ERR_INVALID_ARG&&!live&&!allocations);
 for(int i=0;i<1024;i++)keys[i]=123;
 assert(mpd_client_playlist_remove(name,7,keys,1024,1023)==ESP_OK);release(captured.keys);release(captured.playlist);
 assert(!live);return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / 'request.c').write_text(prefix + types + boundary +
                production(source, 'enqueue') + '\n' +
                production(source, 'mpd_client_playlist_add') + '\n' +
                production(source, 'mpd_client_playlist_remove') + main)
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-I' + str(ROOT / 'app/mpd'), '-I' + str(ROOT / 'shared'),
                            str(path / 'request.c'), str(ROOT / 'app/mpd/mpd_wire.c'),
                            '-o', str(path / 'request')], check=True)
            subprocess.run([str(path / 'request')], check=True)


if __name__ == '__main__':
    unittest.main()
