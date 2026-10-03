#pragma once
#include <Arduino.h>

// OGOTA1: staged, signed, resumable LoRa firmware transfer. This module does not
// use ArduinoOTA and will NEVER apply an unsigned image.
namespace OtaManager {
    // f[0..7] = OGU1|kind|controller|target|transfer_id|parameter|data|hop
    bool handle(const String f[8], String &status, uint32_t &nextOffset);
    // Delayed restart only after a signed image was flashed and X requested.
    void tick();
    // Called after essential services successfully initialize on new firmware.
    void confirmBootIfHealthy();
}
