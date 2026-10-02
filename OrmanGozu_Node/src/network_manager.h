#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

namespace NetworkManager {
    void init();
    void taskLoop(void *pvParameters);
    void requestMaintenanceScan();
}

#endif
