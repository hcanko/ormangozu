#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include "globals.h"

namespace StorageManager {
    bool init();
    bool isReady();
    void logTelemetry(const SensorDataPacket &packet, const char *trigger);
    void logThermalFrame(const SensorDataPacket &packet);
    void logLoraEvent(const char *direction, const String &type, const String &peer,
                      uint32_t sequence, float rssi, float snr, const String &detail);
    void logFusionEvent(const char *result, uint32_t eventSequence,
                        const SensorDataPacket *localPacket, const PeerEvent *peerEvent,
                        const String &detail);
    String statusJson();
    const char* telemetryPath();
    const char* loraLogPath();
    const char* fusionLogPath();
    const char* framePath(uint8_t index = 0);
    void eraseAllLogs();
}

#endif
