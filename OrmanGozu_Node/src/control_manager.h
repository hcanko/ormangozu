#pragma once
#include "globals.h"

// All state changes and actuator operations are owned by SensorTask (Core 0).
// LoraTask and USB NetworkTask only enqueue validated requests.
namespace ControlManager {
  void init();
  void processPending();
  void onSample(const SensorDataPacket &packet);
  void forceAuto(const char *reason);
  bool manualActive();
}
