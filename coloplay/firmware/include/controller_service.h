#pragma once
#include "bluetooth_service.h"
namespace ControllerService {
using Snapshot=RemoteFeedback::Snapshot;
void tick(bool hardwareReady);
// Called by the console's sole RX consumer. true means binary/claimed input.
bool consume(uint8_t byte);
Snapshot snapshot();
bool send(uint8_t kind,uint16_t usage,int count,uint16_t parameter=0,uint16_t duration=0);
void cancel();
String statusJson();
}
