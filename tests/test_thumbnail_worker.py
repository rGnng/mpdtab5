"""Exercise the UI's production thumbnail handoff with deterministic RTOS queues."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_album_view import function

ROOT = Path(__file__).resolve().parents[1]
PREFIX = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#define ESP_PLATFORM 1
#define LV_UNUSED(x) (void)(x)
#define pdTRUE 1
#define pdPASS 1
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define REAL_THUMB_SIZE 54
#define THUMB_CACHE_THUMB_SIZE (54*54*2)
#define COVER_CACHE_SLOTS 4
#define LV_IMAGE_HEADER_MAGIC 25
#define LV_COLOR_FORMAT_RGB565 4
typedef struct { uint16_t id; uint32_t epoch; bool ok; char library[128]; } thumb_io_job_t;
typedef struct { bool full; thumb_io_job_t job; } queue_t;
typedef queue_t *QueueHandle_t;
typedef struct {
    struct {int magic, cf, w, h, stride;} header;
    size_t data_size; const uint8_t *data;
} lv_image_dsc_t;
typedef struct {uint16_t id; uint8_t *data; lv_image_dsc_t dsc;} cover_cache_slot_t;
static cover_cache_slot_t cover_cache[COVER_CACHE_SLOTS];
static int cover_cache_next;
static QueueHandle_t thumb_io_jobs, thumb_io_results;
static uint8_t *thumb_io_pixels;
static uint16_t thumb_io_pending, thumb_io_failed;
static uint32_t thumb_io_epoch, thumb_io_revision, thumb_io_failed_tick, tick;
static atomic_bool thumb_io_active;
static int fail_task, fail_alloc, allocations, task_starts;
static uint32_t lv_tick_get(void) { return tick; }
static uint32_t lv_tick_elaps(uint32_t t) { return tick-t; }
static void *ui_alloc_aligned(size_t n) { allocations++;return fail_alloc?NULL:malloc(n); }
static QueueHandle_t xQueueCreate(int count,size_t size) {(void)count;(void)size;return calloc(1,sizeof(queue_t));}
static void vQueueDelete(QueueHandle_t q) {free(q);}
static int xQueueSend(QueueHandle_t q,const void *job,int timeout) {
    (void)timeout;if(q->full)return 0;q->job=*(const thumb_io_job_t*)job;q->full=true;return pdTRUE;
}
static int xQueueReceive(QueueHandle_t q,void *job,int timeout) {
    (void)timeout;if(!q->full)return 0;*(thumb_io_job_t*)job=q->job;q->full=false;return pdTRUE;
}
static void thumb_io_worker(void *unused) {(void)unused;}
static int xTaskCreateWithCaps(void (*fn)(void*),const char *name,int stack,void *arg,int priority,void *handle,int caps) {
    (void)fn;(void)name;(void)stack;(void)arg;(void)priority;(void)handle;(void)caps;task_starts++;return fail_task?0:pdPASS;
}
static const char *app_config_library_id(void) {return "MPD-test";}
'''
SUFFIX = r'''
void test_reset(void) {
    for(int i=0;i<COVER_CACHE_SLOTS;i++)free(cover_cache[i].data);
    memset(cover_cache,0,sizeof(cover_cache));cover_cache_next=0;
    free(thumb_io_pixels);free(thumb_io_jobs);free(thumb_io_results);
    thumb_io_pixels=NULL;thumb_io_jobs=thumb_io_results=NULL;
    thumb_io_pending=thumb_io_failed=0;thumb_io_epoch=thumb_io_revision=0;
    thumb_io_active=false;tick=1000;fail_task=fail_alloc=allocations=task_starts=0;
}
void test_request(int id) {thumb_io_request(id);}
void test_active(int active) {thumb_io_active=active;}
void test_switch(void) {thumb_io_epoch++;}
void test_tick(int elapsed) {tick+=elapsed;}
void test_fail(int task,int alloc) {fail_task=task;fail_alloc=alloc;}
int test_pending(void) {return thumb_io_pending;}
int test_revision(void) {return thumb_io_revision;}
int test_tasks(void) {return task_starts;}
int test_allocations(void) {return allocations;}
int test_ready(int id) {for(int i=0;i<COVER_CACHE_SLOTS;i++)if(cover_cache[i].id==id)return cover_cache[i].data[0];return -1;}
int test_complete(int ok) {
    thumb_io_job_t job;if(!thumb_io_jobs||!xQueueReceive(thumb_io_jobs,&job,0))return 0;
    if(strcmp(job.library,"MPD-test"))return 0;
    memset(thumb_io_pixels,job.id,THUMB_CACHE_THUMB_SIZE);job.ok=ok;
    xQueueSend(thumb_io_results,&job,0);thumb_io_poll();return 1;
}
'''


class ThumbnailWorkerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.work.cleanup)
        path = Path(cls.work.name)
        source = (ROOT / 'app/ui/ui.c').read_text()
        funcs = '\n'.join(function(source, name) for name in
                          ('thumb_io_start', 'thumb_io_request', 'thumb_io_busy', 'thumb_io_poll'))
        (path / 'worker.c').write_text(PREFIX + funcs + SUFFIX)
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-function', '-shared', '-fPIC', str(path / 'worker.c'),
                        '-o', str(path / 'worker.so')], check=True)
        cls.lib = C.CDLL(str(path / 'worker.so'))

    def setUp(self):
        self.lib.test_reset()

    def test_waits_for_scroll_idle_and_deduplicates_requests(self):
        self.lib.test_active(1)
        self.lib.test_request(7)
        self.assertEqual(self.lib.test_tasks(), 0)
        self.lib.test_active(0)
        self.lib.test_request(7)
        self.lib.test_request(7)
        self.lib.test_request(8)
        self.assertEqual(self.lib.test_pending(), 7)
        self.assertEqual(self.lib.test_tasks(), 1)
        self.assertEqual(self.lib.test_ready(7), -1)
        self.assertEqual(self.lib.test_complete(1), 1)
        self.assertEqual(self.lib.test_ready(7), 7)
        self.assertEqual(self.lib.test_pending(), 0)

    def test_late_result_cannot_enter_another_library(self):
        self.lib.test_request(7)
        self.lib.test_switch()
        self.lib.test_complete(1)
        self.assertEqual(self.lib.test_ready(7), -1)
        self.assertEqual(self.lib.test_revision(), 0)
        self.lib.test_request(8)
        self.lib.test_complete(1)
        self.assertEqual(self.lib.test_ready(8), 8)

    def test_failures_retry_after_cooldown_without_publishing(self):
        self.lib.test_request(7)
        self.lib.test_complete(0)
        self.assertEqual(self.lib.test_revision(), 0)
        self.lib.test_request(7)
        self.assertEqual(self.lib.test_pending(), 0)
        self.lib.test_tick(250)
        self.lib.test_request(7)
        self.lib.test_complete(1)
        self.assertEqual(self.lib.test_ready(7), 7)

    def test_memory_and_task_count_remain_bounded(self):
        for i in range(1, 30):
            self.lib.test_request(i)
            self.lib.test_complete(1)
        self.assertEqual(self.lib.test_allocations(), 5)  # staging + four row slots
        self.assertEqual(self.lib.test_tasks(), 1)
        self.assertEqual(self.lib.test_ready(29), 29)

    def test_task_creation_failure_can_recover(self):
        self.lib.test_fail(1, 0)
        self.lib.test_request(7)
        self.assertEqual(self.lib.test_pending(), 0)
        self.lib.test_fail(0, 0)
        self.lib.test_request(7)
        self.lib.test_complete(1)
        self.assertEqual(self.lib.test_ready(7), 7)


if __name__ == '__main__':
    unittest.main()
