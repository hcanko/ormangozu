#include "algorithm_manager.h"

#include <Arduino.h>
#include <cstddef>
#include <cstring>
#include <math.h>

namespace {
constexpr uint32_t RTC_MAGIC = 0x4F475633;  // "OGV3"
constexpr uint16_t RTC_VERSION = 3;
constexpr float ALPHA_UP = 0.05f;
constexpr float ALPHA_DOWN = 0.001f;

struct RtcAlgorithmState {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    float gasEma;
    float previousTop5Temp;
    int8_t previousHotspotX;
    int8_t previousHotspotY;
    uint16_t previousClusterSize;
    uint8_t persistenceCount;
    uint8_t reserved2[3];
    uint32_t sampleCount;
    uint32_t checksum;
};

RTC_DATA_ATTR RtcAlgorithmState rtcState = {};

uint32_t checksumFor(const RtcAlgorithmState &state) {
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&state);
    const size_t length = offsetof(RtcAlgorithmState, checksum);
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < length; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

bool stateIsValid() {
    return rtcState.magic == RTC_MAGIC &&
           rtcState.version == RTC_VERSION &&
           rtcState.checksum == checksumFor(rtcState);
}

void saveState() {
    rtcState.magic = RTC_MAGIC;
    rtcState.version = RTC_VERSION;
    rtcState.checksum = checksumFor(rtcState);
}

float clampScore(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 100.0f) return 100.0f;
    return value;
}

float calculateTop5Average(const float *pixels) {
    float top[5] = {-1000.0f, -1000.0f, -1000.0f, -1000.0f, -1000.0f};
    for (int i = 0; i < 768; ++i) {
        const float value = pixels[i];
        for (int pos = 0; pos < 5; ++pos) {
            if (value > top[pos]) {
                for (int shift = 4; shift > pos; --shift) top[shift] = top[shift - 1];
                top[pos] = value;
                break;
            }
        }
    }
    float sum = 0.0f;
    for (int i = 0; i < 5; ++i) sum += top[i];
    return sum / 5.0f;
}

float calculateAmbient(const float *pixels) {
    // Küçük sıcak bölgelerin ortam ortalamasını sürüklememesi için iki geçişli kırpılmış ortalama.
    float sum = 0.0f;
    uint16_t count = 0;
    for (int i = 0; i < 768; ++i) {
        if (pixels[i] > -50.0f && pixels[i] < 300.0f) {
            sum += pixels[i];
            count++;
        }
    }
    if (count == 0) return 0.0f;
    const float firstMean = sum / count;

    sum = 0.0f;
    count = 0;
    const float upper = firstMean + 8.0f;
    for (int i = 0; i < 768; ++i) {
        if (pixels[i] > -50.0f && pixels[i] <= upper) {
            sum += pixels[i];
            count++;
        }
    }
    return count > 0 ? sum / count : firstMean;
}

void calculateHotspotMetrics(SensorDataPacket &packet) {
    bool hotMask[768] = {};
    bool visited[768] = {};
    uint16_t queue[768];

    packet.hot_pixels_45 = 0;
    packet.hot_pixels_50 = 0;
    packet.hot_pixels_60 = 0;
    packet.largest_hotspot_cluster = 0;
    packet.hotspot_x = -1;
    packet.hotspot_y = -1;

    for (int i = 0; i < 768; ++i) {
        const float value = packet.pixels[i];
        if (value >= 45.0f) packet.hot_pixels_45++;
        if (value >= 50.0f) packet.hot_pixels_50++;
        if (value >= 60.0f) packet.hot_pixels_60++;
        hotMask[i] = value >= packet.hotspot_threshold;
    }

    for (uint16_t start = 0; start < 768; ++start) {
        if (!hotMask[start] || visited[start]) continue;

        uint16_t head = 0;
        uint16_t tail = 0;
        queue[tail++] = start;
        visited[start] = true;
        uint16_t clusterSize = 0;
        uint32_t sumX = 0;
        uint32_t sumY = 0;

        while (head < tail) {
            const uint16_t index = queue[head++];
            const int x = index % 32;
            const int y = index / 32;
            clusterSize++;
            sumX += static_cast<uint32_t>(x);
            sumY += static_cast<uint32_t>(y);

            const int neighbors[4] = {
                x > 0 ? static_cast<int>(index) - 1 : -1,
                x < 31 ? static_cast<int>(index) + 1 : -1,
                y > 0 ? static_cast<int>(index) - 32 : -1,
                y < 23 ? static_cast<int>(index) + 32 : -1,
            };
            for (int n = 0; n < 4; ++n) {
                const int candidate = neighbors[n];
                if (candidate >= 0 && !visited[candidate] && hotMask[candidate]) {
                    visited[candidate] = true;
                    queue[tail++] = static_cast<uint16_t>(candidate);
                }
            }
        }

        if (clusterSize > packet.largest_hotspot_cluster) {
            packet.largest_hotspot_cluster = clusterSize;
            packet.hotspot_x = static_cast<int8_t>(lroundf(static_cast<float>(sumX) / clusterSize));
            packet.hotspot_y = static_cast<int8_t>(lroundf(static_cast<float>(sumY) / clusterSize));
        }
    }
}

bool sameHotspot(const SensorDataPacket &packet) {
    if (packet.largest_hotspot_cluster < THERMAL_MIN_CLUSTER_PIXELS ||
        packet.hotspot_x < 0 || packet.hotspot_y < 0 ||
        rtcState.previousClusterSize < THERMAL_MIN_CLUSTER_PIXELS ||
        rtcState.previousHotspotX < 0 || rtcState.previousHotspotY < 0) {
        return false;
    }
    const float dx = static_cast<float>(packet.hotspot_x - rtcState.previousHotspotX);
    const float dy = static_cast<float>(packet.hotspot_y - rtcState.previousHotspotY);
    return sqrtf(dx * dx + dy * dy) <= THERMAL_PERSISTENCE_DISTANCE_PX;
}

uint8_t determineFireLevel(const SensorDataPacket &packet) {
    const bool thermalCandidate = packet.mlx_ok && (
        packet.max_temp >= THERMAL_WATCH_MAX_C ||
        packet.top5_temp >= THERMAL_HOTSPOT_MIN_C ||
        packet.largest_hotspot_cluster >= THERMAL_MIN_CLUSTER_PIXELS
    );
    const bool strongThermal = packet.mlx_ok && (
        packet.max_temp >= THERMAL_STRONG_MAX_C ||
        packet.top5_temp >= 60.0f ||
        packet.largest_hotspot_cluster >= THERMAL_STRONG_CLUSTER_PIXELS
    );
    const bool persistentThermal = thermalCandidate &&
        packet.persistence_count >= THERMAL_REQUIRED_PERSISTENCE;
    const bool gasSupport = packet.gas_ok && packet.gas_drop_pct >= GAS_SUPPORT_DROP_PCT;

    if (packet.mlx_ok && packet.max_temp >= THERMAL_OVERRIDE_CRITICAL_C) return FIRE_CRITICAL;
    if ((strongThermal && persistentThermal) || packet.local_fire_score >= FUSION_CRITICAL_SCORE) {
        return FIRE_CRITICAL;
    }
    if ((persistentThermal && (strongThermal || packet.local_fire_score >= 50.0f)) ||
        (thermalCandidate && gasSupport) ||
        packet.local_fire_score >= FUSION_WARNING_SCORE) {
        return FIRE_WARNING;
    }
    if (thermalCandidate || gasSupport || packet.local_fire_score >= FUSION_WATCH_SCORE) {
        return FIRE_WATCH;
    }
    return FIRE_NORMAL;
}
}  // namespace

namespace AlgorithmManager {

void init() {
    if (!stateIsValid()) {
        resetState();
        Serial.println("[ALGORITHM] RTC state initialized (v3 thermal-first).");
    } else {
        Serial.printf("[ALGORITHM] RTC restored. samples=%lu gasEMA=%.1f persistence=%u\n",
                      static_cast<unsigned long>(rtcState.sampleCount),
                      rtcState.gasEma,
                      rtcState.persistenceCount);
    }
}

void resetState() {
    memset(&rtcState, 0, sizeof(rtcState));
    rtcState.magic = RTC_MAGIC;
    rtcState.version = RTC_VERSION;
    rtcState.previousHotspotX = -1;
    rtcState.previousHotspotY = -1;
    saveState();
}

const char *fireLevelName(uint8_t level) {
    switch (level) {
        case FIRE_WATCH: return "WATCH";
        case FIRE_WARNING: return "WARNING";
        case FIRE_CRITICAL: return "CRITICAL";
        case FIRE_NETWORK_CORROBORATED: return "NETWORK_CORROBORATED";
        default: return "NORMAL";
    }
}

const char *healthLevelName(uint8_t level) {
    switch (level) {
        case HEALTH_DEGRADED: return "DEGRADED";
        case HEALTH_FAULT: return "FAULT";
        default: return "OK";
    }
}

void analyze(SensorDataPacket &packet, float elapsedSeconds) {
    if (!stateIsValid()) resetState();
    if (elapsedSeconds <= 0.001f) elapsedSeconds = 1.0f;

    packet.health_level = packet.mlx_ok && packet.gas_ok
        ? HEALTH_OK
        : ((packet.mlx_ok || packet.gas_ok) ? HEALTH_DEGRADED : HEALTH_FAULT);

    if (packet.mlx_ok) {
        packet.ambient_temp = calculateAmbient(packet.pixels);
        packet.hotspot_threshold = max(
            THERMAL_HOTSPOT_MIN_C,
            packet.ambient_temp + THERMAL_HOTSPOT_DELTA_ABOVE_AMBIENT_C
        );
        packet.top5_temp = calculateTop5Average(packet.pixels);
        calculateHotspotMetrics(packet);

        packet.local_delta_t = 0.0f;
        if (rtcState.sampleCount > 0) {
            packet.local_delta_t = (packet.top5_temp - rtcState.previousTop5Temp) / elapsedSeconds;
        }

        if (sameHotspot(packet)) {
            rtcState.persistenceCount = rtcState.persistenceCount < 10 ? rtcState.persistenceCount + 1 : 10;
        } else if (packet.largest_hotspot_cluster >= THERMAL_MIN_CLUSTER_PIXELS) {
            rtcState.persistenceCount = 1;
        } else {
            rtcState.persistenceCount = 0;
        }
        packet.persistence_count = rtcState.persistenceCount;
    } else {
        packet.ambient_temp = 0.0f;
        packet.hotspot_threshold = THERMAL_HOTSPOT_MIN_C;
        packet.top5_temp = 0.0f;
        packet.local_delta_t = 0.0f;
        packet.persistence_count = 0;
        packet.hotspot_x = -1;
        packet.hotspot_y = -1;
    }

    if (packet.gas_res > 0.0f && packet.gas_ok) {
        if (rtcState.gasEma <= 0.0f) {
            rtcState.gasEma = packet.gas_res;
        } else if (packet.gas_res > rtcState.gasEma) {
            rtcState.gasEma = ALPHA_UP * packet.gas_res + (1.0f - ALPHA_UP) * rtcState.gasEma;
        } else {
            rtcState.gasEma = ALPHA_DOWN * packet.gas_res + (1.0f - ALPHA_DOWN) * rtcState.gasEma;
        }
    }

    packet.gas_ema = rtcState.gasEma;
    packet.gas_drop_pct = 0.0f;
    if (packet.gas_ok && packet.gas_res > 0.0f && rtcState.gasEma > 0.0f) {
        packet.gas_drop_pct = ((rtcState.gasEma - packet.gas_res) / rtcState.gasEma) * 100.0f;
        packet.gas_drop_pct = constrain(packet.gas_drop_pct, 0.0f, 100.0f);
    }

    packet.score_temp = packet.mlx_ok
        ? clampScore((packet.top5_temp - 30.0f) * (100.0f / 45.0f))
        : 0.0f;
    const float clusterSizeScore = clampScore(
        static_cast<float>(packet.largest_hotspot_cluster) *
        (100.0f / THERMAL_CLUSTER_FULL_SCORE_PIXELS)
    );
    const float veryHotPixelScore = clampScore(static_cast<float>(packet.hot_pixels_60) * 20.0f);
    packet.score_cluster = max(clusterSizeScore, veryHotPixelScore);
    packet.score_persistence = packet.persistence_count >= THERMAL_REQUIRED_PERSISTENCE ? 100.0f : 0.0f;
    packet.score_delta_t = clampScore(packet.local_delta_t * 50.0f);
    packet.score_gas = clampScore(packet.gas_drop_pct * 2.0f);

    packet.local_fire_score = clampScore(
        packet.score_temp * FUSION_WEIGHT_TEMP +
        packet.score_cluster * FUSION_WEIGHT_CLUSTER +
        packet.score_persistence * FUSION_WEIGHT_PERSISTENCE +
        packet.score_delta_t * FUSION_WEIGHT_DELTA_T +
        packet.score_gas * FUSION_WEIGHT_GAS
    );
    packet.fire_level = determineFireLevel(packet);
    packet.network_confirmed = false;

    if (packet.mlx_ok) {
        rtcState.previousTop5Temp = packet.top5_temp;
        rtcState.previousHotspotX = packet.hotspot_x;
        rtcState.previousHotspotY = packet.hotspot_y;
        rtcState.previousClusterSize = packet.largest_hotspot_cluster;
    } else {
        rtcState.previousHotspotX = -1;
        rtcState.previousHotspotY = -1;
        rtcState.previousClusterSize = 0;
        rtcState.persistenceCount = 0;
    }
    rtcState.sampleCount++;
    saveState();
}

}  // namespace AlgorithmManager
