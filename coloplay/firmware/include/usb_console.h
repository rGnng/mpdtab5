#pragma once
#include <Arduino.h>
namespace UsbConsole {
Stream &stream();
bool prepareNetwork();
void useNetwork(bool enabled);
void tick();
bool connected();
uint32_t dropped();
size_t queued();
// Append a control frame completely or reject it; never emit a partial frame.
bool writeFrame(const uint8_t *data,size_t size);
}
