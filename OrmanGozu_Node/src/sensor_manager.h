#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <Arduino.h>
#include "globals.h"

namespace SensorManager {
    void init();
    void taskLoop(void *pvParameters);
    bool enqueuePeerEvent(const PeerEvent &event);
    bool isFastMode();
    bool enqueueControlRequest(const ControlRequest &request);
    void requestImmediateSample();
    void requestFastMode();
    String statusJson();
}

#endif
