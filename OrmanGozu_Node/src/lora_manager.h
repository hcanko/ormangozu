#ifndef LORA_MANAGER_H
#define LORA_MANAGER_H

namespace LoraManager {
    void init();
    void taskLoop(void *pvParameters);
    bool listenBeforeTalk();
}

#endif