#pragma once

#include "config.h"

#if WIFI_BRIDGE_ENABLE

#include <WiFi.h>

namespace WifiBridge {
    // Cliente único TCP — quando Mission Planner conecta, fica aqui.
    // Acesso de leitura/escrita do osd_task (TX) e wifi_task (RX).
    extern WiFiClient client;

    // Mutex pra serializar writes (osd_task pode chamar de um core, wifi_task de outro)
    extern SemaphoreHandle_t mutex;

    inline bool isConnected() {
        return client && client.connected();
    }

    // Manda bytes pro cliente conectado (no-op se sem cliente).
    inline void write(const uint8_t* buf, uint16_t len) {
        if (!isConnected()) return;
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            client.write(buf, len);
            xSemaphoreGive(mutex);
        }
    }
}

#else  // Wi-Fi desligado — versões no-op

namespace WifiBridge {
    inline bool isConnected() { return false; }
    inline void write(const uint8_t*, uint16_t) {}
}

#endif
