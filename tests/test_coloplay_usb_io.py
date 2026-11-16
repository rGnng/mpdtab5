"""USB I/O must keep pumping when the HTTP/main loop stops ticking."""
from pathlib import Path
import re,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
PREFIX=r'''
#include <atomic>
#include <deque>
#include <cstdint>
#include <cassert>
#include <cstdio>
using QueueHandle_t=std::deque<uint8_t>*;
using TaskHandle_t=void*;
#define pdPASS 1
#define pdTRUE 1
#define pdMS_TO_TICKS(n) (n)
#define vTaskDelay(n) ((void)(n))
static unsigned pumps,drains;
static bool ready,task_ok=true;
static QueueHandle_t xQueueCreate(unsigned cap,unsigned size){assert(cap==512&&size==1);return new std::deque<uint8_t>;}
static int xQueueSend(QueueHandle_t q,const uint8_t *c,int wait){assert(!wait);if(q->size()==512)return 0;q->push_back(*c);return 1;}
static int xQueueReceive(QueueHandle_t q,uint8_t *c,int wait){assert(!wait);if(q->empty())return 0;*c=q->front();q->pop_front();return 1;}
static int xTaskCreatePinnedToCore(void(*fn)(void*),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *out,unsigned core){
 assert(fn&&name&&stack==4096&&!arg&&priority==3&&core==1);if(!task_ok)return 0;*out=(void*)1;return 1;
}
namespace DeviceLog {void println(const char*){}}
namespace ControllerService {
 bool consume(uint8_t c){return c>=128;}
 void tick(bool value){pumps++;ready=value;}
}
namespace UsbConsole {
 struct Port {std::deque<uint8_t> rx;int available(){return rx.size();}int read(){if(rx.empty())return -1;int c=rx.front();rx.pop_front();return c;}} port;
 Port &stream(){return port;}
 void tick(){drains++;}
}
'''
SUFFIX=r'''
int main(){
 ControllerIo::begin();ControllerIo::tick(true);assert(!pumps);
 for(auto c:{'s','t','a','t','u','s','\n'})UsbConsole::port.rx.push_back(c);
 UsbConsole::port.rx.push_back(0xc7);
 // No further main-loop ticks: the dedicated worker still receives and drains.
 for(unsigned n=0;n<2000;++n)ControllerIo::pump();
 assert(pumps==2000&&drains==2000&&ready);
 for(auto c:{'s','t','a','t','u','s','\n'})assert(ControllerIo::consoleRead()==c);
 assert(ControllerIo::consoleRead()==-1);
 for(unsigned n=0;n<2048;++n)UsbConsole::port.rx.push_back(0xc7);
 ControllerIo::pump();assert(UsbConsole::port.rx.size()==1024);
 ControllerIo::pump();assert(UsbConsole::port.rx.empty());
 ControllerIo::tick(false);ControllerIo::pump();assert(!ready);
 // Allocation/task failure keeps console and USB framing available on main.
 ControllerIo::gTask=nullptr;task_ok=false;ControllerIo::begin();assert(!ControllerIo::gTask);
 UsbConsole::port.rx={0xc7,'s'};assert(ControllerIo::consoleRead()=='s');
 ControllerIo::tick(true);assert(ready);
 puts("PASS dedicated USB worker pumps without main-loop ticks, bounded RX batches, text-console queue and allocation fallback");
}
'''
class ColoPlayUsbIoTests(unittest.TestCase):
 def test_usb_worker_does_not_depend_on_http_loop(self):
  source=(ROOT/'coloplay/firmware/src/controller_io.cpp').read_text();source=re.sub(r'^#include .*$', '',source,flags=re.M)
  with tempfile.TemporaryDirectory() as tmp:
   path=Path(tmp);(path/'io.cpp').write_text(PREFIX+source+SUFFIX)
   subprocess.run(['c++','-std=c++11','-Wall','-Wextra','-Werror',str(path/'io.cpp'),'-o',str(path/'io')],check=True)
   subprocess.run([str(path/'io')],check=True)
if __name__=='__main__':unittest.main()
