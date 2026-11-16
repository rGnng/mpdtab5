#include "controller_io.h"
#include "controller_service.h"
#include "usb_console.h"
#include "device_log.h"
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
namespace ControllerIo {
namespace {
QueueHandle_t gConsole=nullptr;
TaskHandle_t gTask=nullptr;
std::atomic<bool> gReady{false};
void pump(){
  // Bound each batch so a noisy host cannot starve heartbeat deadlines.
  for(unsigned n=0;n<1024&&UsbConsole::stream().available()>0;++n){
    const int c=UsbConsole::stream().read();if(c<0)break;
    if(!ControllerService::consume(uint8_t(c))&&gConsole){const uint8_t byte=c;xQueueSend(gConsole,&byte,0);}
  }
  ControllerService::tick(gReady.load());
  UsbConsole::tick();
}
void worker(void *){for(;;){pump();vTaskDelay(pdMS_TO_TICKS(2));}}
}
void begin(){
  if(gTask)return;
  if(!gConsole)gConsole=xQueueCreate(512,sizeof(uint8_t));
  if(gConsole&&xTaskCreatePinnedToCore(worker,"controller_io",4096,nullptr,3,&gTask,1)==pdPASS)return;
  gTask=nullptr;DeviceLog::println("[controller] USB worker unavailable; using main loop");
}
void tick(bool hardwareReady){
  gReady.store(hardwareReady);
  if(!gTask){ControllerService::tick(hardwareReady);UsbConsole::tick();}
}
int consoleRead(){
  if(!gTask){
    for(unsigned n=0;n<1024;++n){
      const int c=UsbConsole::stream().read();if(c<0)return -1;
      if(!ControllerService::consume(uint8_t(c)))return c;
    }
    return -1;
  }
  uint8_t byte;return xQueueReceive(gConsole,&byte,0)==pdTRUE?byte:-1;
}
}
