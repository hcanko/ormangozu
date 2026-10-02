#include "storage_manager.h"
#include "algorithm_manager.h"

#include <LittleFS.h>
#include <math.h>

namespace {
bool ready = false;
SemaphoreHandle_t storageMutex = nullptr;
const char *TELEMETRY_PATH = "/telemetry_0.csv";
const char *LORA_PATH = "/lora_0.csv";
const char *FUSION_PATH = "/fusion_0.csv";
const char *FRAME_PATHS[FRAME_RING_FILE_COUNT] = {
    "/frames_0.bin", "/frames_1.bin", "/frames_2.bin"
};

uint16_t crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (uint8_t b = 0; b < 8; ++b) {
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

void rotateTextFile(const char *path0, const char *path1, size_t maxBytes) {
    File f = LittleFS.open(path0, FILE_READ);
    const size_t size = f ? f.size() : 0;
    if (f) f.close();
    if (size < maxBytes) return;
    LittleFS.remove(path1);
    LittleFS.rename(path0, path1);
}

void rotateFramesIfNeeded() {
    File f = LittleFS.open(FRAME_PATHS[0], FILE_READ);
    const size_t size = f ? f.size() : 0;
    if (f) f.close();
    if (size < FRAME_ROTATE_BYTES) return;

    LittleFS.remove(FRAME_PATHS[FRAME_RING_FILE_COUNT - 1]);
    for (int i = FRAME_RING_FILE_COUNT - 1; i > 0; --i) {
        LittleFS.rename(FRAME_PATHS[i - 1], FRAME_PATHS[i]);
    }
}

void ensureHeaders() {
    if (!LittleFS.exists(TELEMETRY_PATH)) {
        File f = LittleFS.open(TELEMETRY_PATH, FILE_WRITE);
        if (f) {
            f.println(
                "protocol_version,firmware_version,boot_id,tower_id,sequence,uptime_ms,trigger,"
                "max_temp,ambient_temp,hotspot_threshold,top5_temp,hot_pixels_45,hot_pixels_50,"
                "hot_pixels_60,largest_hotspot_cluster,hotspot_x,hotspot_y,persistence_count,"
                "delta_t,gas_raw,gas_ema,gas_drop_pct,score_temp,score_cluster,score_persistence,"
                "score_delta_t,score_gas,fire_score,fire_level,fire_status,health_level,health_status,"
                "network_confirmed,battery_mv,battery_pct,solar_voltage_mv,solar_current_ma,mlx_ok,"
                "gas_ok,error_counter,free_heap,lat,lng"
            );
            f.close();
        }
    }
    if (!LittleFS.exists(LORA_PATH)) {
        File f = LittleFS.open(LORA_PATH, FILE_WRITE);
        if (f) {
            f.println("uptime_ms,direction,type,peer,sequence,rssi,snr,detail");
            f.close();
        }
    }
    if (!LittleFS.exists(FUSION_PATH)) {
        File f = LittleFS.open(FUSION_PATH, FILE_WRITE);
        if (f) {
            f.println(
                "uptime_ms,local_tower_id,result,event_sequence,local_level,local_score,local_max_temp,local_cluster,"
                "local_persistence,peer_id,peer_level,peer_score,peer_max_temp,peer_cluster,"
                "peer_persistence,peer_gas_drop,rssi,snr,detail"
            );
            f.close();
        }
    }
}

#pragma pack(push, 1)
struct FrameHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t pixel_count;
    uint32_t sequence;
    uint32_t uptime_ms;
    int16_t max_temp_x10;
    int16_t ambient_temp_x10;
    uint16_t largest_cluster;
    int8_t hotspot_x;
    int8_t hotspot_y;
    uint8_t fire_level;
    uint8_t persistence_count;
    uint16_t pixel_crc;
};
#pragma pack(pop)
} // namespace

namespace StorageManager {

bool init() {
#if ENABLE_LITTLEFS_LOGGING
    storageMutex = xSemaphoreCreateMutex();
    ready = LittleFS.begin(true);
    if (!ready) {
        Serial.println("❌ LittleFS başlatılamadı.");
        return false;
    }
    ensureHeaders();
    Serial.printf("✅ LittleFS: toplam=%u, kullanılan=%u bayt\n",
                  static_cast<unsigned>(LittleFS.totalBytes()),
                  static_cast<unsigned>(LittleFS.usedBytes()));
    return true;
#else
    return false;
#endif
}

bool isReady() { return ready; }

void logTelemetry(const SensorDataPacket &p, const char *trigger) {
    if (!ready || xSemaphoreTake(storageMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return;
    rotateTextFile(TELEMETRY_PATH, "/telemetry_1.csv", TELEMETRY_ROTATE_BYTES);
    ensureHeaders();
    File f = LittleFS.open(TELEMETRY_PATH, FILE_APPEND);
    if (f) {
        f.printf(
            "%u,%s,%s,%s,%lu,%lu,%s,%.2f,%.2f,%.2f,%.2f,%u,%u,%u,%u,%d,%d,%u,"
            "%.4f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%u,%s,%u,%s,%d,"
            "%.1f,%d,%.1f,%.1f,%d,%d,%lu,%lu,%.6f,%.6f\n",
            PROTOCOL_VERSION, FIRMWARE_VERSION, myBootID.c_str(), myTowerID.c_str(),
            static_cast<unsigned long>(p.sequence), static_cast<unsigned long>(p.uptime_ms), trigger,
            p.max_temp, p.ambient_temp, p.hotspot_threshold, p.top5_temp,
            static_cast<unsigned>(p.hot_pixels_45), static_cast<unsigned>(p.hot_pixels_50),
            static_cast<unsigned>(p.hot_pixels_60), static_cast<unsigned>(p.largest_hotspot_cluster),
            static_cast<int>(p.hotspot_x), static_cast<int>(p.hotspot_y),
            static_cast<unsigned>(p.persistence_count),
            p.local_delta_t, p.gas_res, p.gas_ema, p.gas_drop_pct,
            p.score_temp, p.score_cluster, p.score_persistence, p.score_delta_t, p.score_gas,
            p.local_fire_score, static_cast<unsigned>(p.fire_level), AlgorithmManager::fireLevelName(p.fire_level),
            static_cast<unsigned>(p.health_level), AlgorithmManager::healthLevelName(p.health_level), p.network_confirmed,
            p.battery_mv, p.battery_pct, p.solar_voltage_mv, p.solar_current_ma,
            p.mlx_ok, p.gas_ok, static_cast<unsigned long>(p.error_counter),
            static_cast<unsigned long>(p.free_heap), p.lat, p.lng
        );
        f.close();
    }
    xSemaphoreGive(storageMutex);
}

void logThermalFrame(const SensorDataPacket &p) {
    if (!ready || !p.mlx_ok || xSemaphoreTake(storageMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return;
    rotateFramesIfNeeded();

    int16_t packedPixels[768];
    for (int i = 0; i < 768; ++i) {
        const float clamped = constrain(p.pixels[i], -100.0f, 500.0f);
        packedPixels[i] = static_cast<int16_t>(lroundf(clamped * 10.0f));
    }

    FrameHeader h = {};
    h.magic = 0x4F474652; // OGFR
    h.version = 2;
    h.pixel_count = 768;
    h.sequence = p.sequence;
    h.uptime_ms = p.uptime_ms;
    h.max_temp_x10 = static_cast<int16_t>(lroundf(p.max_temp * 10.0f));
    h.ambient_temp_x10 = static_cast<int16_t>(lroundf(p.ambient_temp * 10.0f));
    h.largest_cluster = p.largest_hotspot_cluster;
    h.hotspot_x = p.hotspot_x;
    h.hotspot_y = p.hotspot_y;
    h.fire_level = p.fire_level;
    h.persistence_count = p.persistence_count;
    h.pixel_crc = crc16(reinterpret_cast<const uint8_t*>(packedPixels), sizeof(packedPixels));

    File f = LittleFS.open(FRAME_PATHS[0], FILE_APPEND);
    if (f) {
        f.write(reinterpret_cast<const uint8_t*>(&h), sizeof(h));
        f.write(reinterpret_cast<const uint8_t*>(packedPixels), sizeof(packedPixels));
        f.close();
    }
    xSemaphoreGive(storageMutex);
}

void logLoraEvent(const char *direction, const String &type, const String &peer,
                  uint32_t sequence, float rssi, float snr, const String &detail) {
    if (!ready || xSemaphoreTake(storageMutex, pdMS_TO_TICKS(500)) != pdTRUE) return;
    rotateTextFile(LORA_PATH, "/lora_1.csv", LORA_LOG_ROTATE_BYTES);
    ensureHeaders();
    File f = LittleFS.open(LORA_PATH, FILE_APPEND);
    if (f) {
        String clean = detail;
        clean.replace(',', ';');
        clean.replace('\n', ' ');
        f.printf("%lu,%s,%s,%s,%lu,%.1f,%.1f,%s\n",
                 static_cast<unsigned long>(millis()), direction, type.c_str(), peer.c_str(),
                 static_cast<unsigned long>(sequence), rssi, snr, clean.c_str());
        f.close();
    }
    xSemaphoreGive(storageMutex);
}

void logFusionEvent(const char *result, uint32_t eventSequence,
                    const SensorDataPacket *localPacket, const PeerEvent *peerEvent,
                    const String &detail) {
    if (!ready || xSemaphoreTake(storageMutex, pdMS_TO_TICKS(500)) != pdTRUE) return;
    rotateTextFile(FUSION_PATH, "/fusion_1.csv", FUSION_LOG_ROTATE_BYTES);
    ensureHeaders();
    File f = LittleFS.open(FUSION_PATH, FILE_APPEND);
    if (f) {
        String clean = detail;
        clean.replace(',', ';');
        clean.replace('\n', ' ');
        f.printf(
            "%lu,%s,%s,%lu,%u,%.2f,%.2f,%u,%u,%s,%u,%.2f,%.2f,%u,%u,%.2f,%.1f,%.1f,%s\n",
            static_cast<unsigned long>(millis()), myTowerID.c_str(), result,
            static_cast<unsigned long>(eventSequence),
            static_cast<unsigned>(localPacket ? localPacket->fire_level : 0),
            localPacket ? localPacket->local_fire_score : 0.0f,
            localPacket ? localPacket->max_temp : 0.0f,
            static_cast<unsigned>(localPacket ? localPacket->largest_hotspot_cluster : 0),
            static_cast<unsigned>(localPacket ? localPacket->persistence_count : 0),
            peerEvent ? peerEvent->source : "",
            static_cast<unsigned>(peerEvent ? peerEvent->fire_level : 0),
            peerEvent ? peerEvent->fire_score : 0.0f,
            peerEvent ? peerEvent->max_temp : 0.0f,
            static_cast<unsigned>(peerEvent ? peerEvent->largest_cluster : 0),
            static_cast<unsigned>(peerEvent ? peerEvent->persistence_count : 0),
            peerEvent ? peerEvent->gas_drop_pct : 0.0f,
            peerEvent ? peerEvent->rssi : 0.0f,
            peerEvent ? peerEvent->snr : 0.0f,
            clean.c_str()
        );
        f.close();
    }
    xSemaphoreGive(storageMutex);
}

String statusJson() {
    String s = "{";
    s += "\"tower_id\":\"" + myTowerID + "\",";
    s += "\"boot_id\":\"" + myBootID + "\",";
    s += "\"firmware\":\"" FIRMWARE_VERSION "\",";
    s += "\"protocol_version\":" + String(PROTOCOL_VERSION) + ",";
    s += "\"storage_ready\":" + String(ready ? "true" : "false") + ",";
    s += "\"total_bytes\":" + String(ready ? LittleFS.totalBytes() : 0) + ",";
    s += "\"used_bytes\":" + String(ready ? LittleFS.usedBytes() : 0);
    s += "}";
    return s;
}

const char* telemetryPath() { return TELEMETRY_PATH; }
const char* loraLogPath() { return LORA_PATH; }
const char* fusionLogPath() { return FUSION_PATH; }
const char* framePath(uint8_t index) {
    return index < FRAME_RING_FILE_COUNT ? FRAME_PATHS[index] : FRAME_PATHS[0];
}

void eraseAllLogs() {
    if (!ready || xSemaphoreTake(storageMutex, pdMS_TO_TICKS(2000)) != pdTRUE) return;
    LittleFS.remove("/telemetry_0.csv");
    LittleFS.remove("/telemetry_1.csv");
    LittleFS.remove("/lora_0.csv");
    LittleFS.remove("/lora_1.csv");
    LittleFS.remove("/fusion_0.csv");
    LittleFS.remove("/fusion_1.csv");
    for (uint8_t i = 0; i < FRAME_RING_FILE_COUNT; ++i) LittleFS.remove(FRAME_PATHS[i]);
    ensureHeaders();
    xSemaphoreGive(storageMutex);
}

} // namespace StorageManager
