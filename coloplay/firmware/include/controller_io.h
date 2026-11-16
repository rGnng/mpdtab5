#pragma once
namespace ControllerIo {
// Start after USB mode initialization; one task owns serial RX/TX thereafter.
void begin();
void tick(bool hardwareReady);
int consoleRead();
}
