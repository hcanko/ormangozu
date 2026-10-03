#include "control_manager.h"
#include "lora_manager.h"
#include "sensor_manager.h"
#include "storage_manager.h"
#include <cstring>
#include <cmath>

// Pilot: physical servo PWM is DISABLED until wiring, limits and power rail are verified.
// Commands remain protocol-compatible, but movement commands return UNSUPPORTED.
#if OG_ENABLE_PAN_TILT
  #include <esp32-hal-ledc.h>
  #include <esp_arduino_version.h>
#endif

namespace {
  bool manual = false;
  uint32_t manualDeadline = 0;
  int pan = 90;
  int tilt = 90;
  SensorDataPacket latest = {};
  bool haveLatest = false;
  bool awaitingSample = false;
  ControlRequest sampleRequester = {};

#if OG_ENABLE_PAN_TILT
  constexpr uint8_t pwmResolution = 16;
  void pwmWrite(uint8_t channel, int pin, int degrees) {
    const uint32_t us = 1000UL + static_cast<uint32_t>(degrees) * 1000UL / 180UL;
    const uint32_t duty = us * 65535UL / 20000UL;
    #if ESP_ARDUINO_VERSION_MAJOR >= 3
      ledcWrite(pin, duty);
    #else
      ledcWrite(channel, duty);
    #endif
  }
  void moveHardware() {
    pwmWrite(6, OG_PAN_SERVO_PIN, pan);
    pwmWrite(7, OG_TILT_SERVO_PIN, tilt);
  }
#endif

  void report(const ControlRequest &request, const char *status) {
    LoraManager::queueControlResult(request, status, haveLatest ? &latest : nullptr,
                                     pan, tilt, manual);
    StorageManager::logLoraEvent("CTRL", String(request.opcode), String(request.source),
                                  request.sequence, 0, 0, String(status));
  }
  void autoMode() {
    manual = false;
    manualDeadline = 0;
    pan = 90; tilt = 90;
    #if OG_ENABLE_PAN_TILT
      moveHardware();
    #endif
  }
}

namespace ControlManager {
void init() {
#if OG_ENABLE_PAN_TILT
  static_assert(OG_PAN_SERVO_PIN >= 0 && OG_TILT_SERVO_PIN >= 0 &&
                OG_PAN_SERVO_PIN != OG_TILT_SERVO_PIN, "Set distinct verified servo GPIOs");
  #if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(OG_PAN_SERVO_PIN, 50, pwmResolution);
    ledcAttach(OG_TILT_SERVO_PIN, 50, pwmResolution);
  #else
    ledcSetup(6, 50, pwmResolution); ledcAttachPin(OG_PAN_SERVO_PIN, 6);
    ledcSetup(7, 50, pwmResolution); ledcAttachPin(OG_TILT_SERVO_PIN, 7);
  #endif
  moveHardware();
#endif
}

void forceAuto(const char *reason) {
  if (!manual) return;
  autoMode();
  Serial.printf("[CTRL] manual override ended: %s\n", reason);
}
bool manualActive() { return manual; } // SensorTask-only call

void processPending() {
  if (manual && static_cast<int32_t>(millis() - manualDeadline) >= 0) forceAuto("timeout");
  ControlRequest request = {};
  while (remoteControlQueue != nullptr && xQueueReceive(remoteControlQueue, &request, 0) == pdPASS) {
    const String op(request.opcode);
    // No remote operation may disable fire detection or erase data.
    if (op == "STATUS") { report(request, "OK"); continue; }
    if (op == "SAMPLE" || op == "FAST") {
      if (awaitingSample) { report(request, "BUSY"); continue; }
      if (op == "FAST") SensorManager::requestFastMode();
      sampleRequester = request;
      awaitingSample = true;
      SensorManager::requestImmediateSample();
      continue;
    }
    if (op == "AUTO") { autoMode(); report(request, "OK"); continue; }
#if OG_ENABLE_PAN_TILT
    const bool unsafe = haveLatest && latest.fire_level >= FIRE_WATCH;
    if (unsafe && (op == "MANUAL" || op == "PAN" || op == "TILT" || op == "HOME")) {
      forceAuto("fire watch"); report(request, "DENIED_FIRE"); continue;
    }
    if (op == "MANUAL") {
      manual = true;
      manualDeadline = millis() + OG_MANUAL_TIMEOUT_MS;
      report(request, "OK"); continue;
    }
    if (op == "HOME") { autoMode(); report(request, "OK"); continue; }
    if (op == "PAN" || op == "TILT") {
      if (!manual) { report(request, "DENIED_MODE"); continue; }
      if (request.argument < OG_SERVO_MIN_DEGREES || request.argument > OG_SERVO_MAX_DEGREES) {
        report(request, "INVALID_ANGLE"); continue;
      }
      if (op == "PAN") pan = request.argument; else tilt = request.argument;
      moveHardware();
      manualDeadline = millis() + OG_MANUAL_TIMEOUT_MS;
      report(request, "OK"); continue;
    }
#else
    if (op == "MANUAL" || op == "PAN" || op == "TILT" || op == "HOME") {
      report(request, "UNSUPPORTED"); continue;
    }
#endif
    report(request, "INVALID_COMMAND");
  }
}

void onSample(const SensorDataPacket &packet) {
  // Local detection always takes priority over the remote camera operator.
  latest = packet;
  haveLatest = true;
  if (packet.fire_level >= FIRE_WATCH) forceAuto("local fire watch");
  if (awaitingSample) {
    awaitingSample = false;
    report(sampleRequester, packet.mlx_ok ? "OK" : "SENSOR_FAULT");
  }
}
} // namespace ControlManager
