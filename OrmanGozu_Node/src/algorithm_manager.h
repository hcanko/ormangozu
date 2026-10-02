#ifndef ALGORITHM_MANAGER_H
#define ALGORITHM_MANAGER_H

#include "globals.h"

namespace AlgorithmManager {
    void init();
    void analyze(SensorDataPacket &packet, float elapsedSeconds);
    void resetState();
    const char *fireLevelName(uint8_t level);
    const char *healthLevelName(uint8_t level);
}

#endif
