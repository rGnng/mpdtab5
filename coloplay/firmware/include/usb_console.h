#pragma once
#include <Arduino.h>
namespace UsbConsole {
constexpr size_t kRxBufferSize=2048;
Stream &stream();
bool prepareSerial();
bool prepareNetwork();
void useNetwork(bool enabled);
void tick();
bool connected();
uint32_t dropped();
size_t queued();
// Append a control frame completely or reject it; never emit a partial frame.
bool writeFrame(const uint8_t *data,size_t size);
}
