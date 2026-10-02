#ifndef LORA_MANAGER_H
#define LORA_MANAGER_H

#include "globals.h"

namespace LoraManager {
    bool init();
    void taskLoop(void *pvParameters);
    bool queueLocalAlert(const SensorDataPacket &packet);
    bool queuePeerReport(const SensorDataPacket &packet, const String &triggerSource, uint32_t triggerSequence);
    bool queueConfirmation(const SensorDataPacket &packet, const String &peer, uint32_t eventSequence);
    bool isReady();
    bool isIdleForLightSleep();
    void notifyIfIrqLineActive();
}

#endif
