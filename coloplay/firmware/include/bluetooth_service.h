#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include "remote_feedback.h"
namespace BluetoothService {
using Snapshot=RemoteFeedback::Snapshot;
void load();
void tick(bool hardwareReady);
bool configure(WebServer &,String &error);
String statusJson();
Snapshot snapshot();
bool send(uint8_t kind,uint16_t usage,int count,uint16_t parameter=0,uint16_t duration=0);
void cancel();
// Drop pending inputs/prediction when the controller changes transport.
void resetInputs();
}
