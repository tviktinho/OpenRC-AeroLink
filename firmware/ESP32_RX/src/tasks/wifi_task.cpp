#include "wifi_task.h"
#include "mavlink_rx_task.h"
#include "../config.h"
#include "../wifi_bridge.h"

#if WIFI_BRIDGE_ENABLE

#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ============================================================================
// wifi_task — ponte MAVLink TCP por Wi-Fi.
//
// AP mode: ESP32 cria a rede "OpenRC-AeroLink", IP fixo 192.168.4.1.
//   No Mission Planner: conecta TCP em 192.168.4.1:5760.
//
// STA mode: ESP32 conecta numa rede existente (definida em config.h).
//   IP aparece no Serial monitor durante boot.
//
// O TX (osd_task) usa WifiBridge::write() pra espelhar cada frame MAVLink
// pro cliente. O RX (este task) lê bytes do cliente e alimenta o parser
// do mavlink_rx_task no canal MAVLINK_COMM_2.
// ============================================================================

namespace WifiBridge {
    WiFiClient client;
    SemaphoreHandle_t mutex = nullptr;
}

namespace WifiTask {
namespace {

WiFiServer s_server(WIFI_TCP_PORT);

void setupAp() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
    delay(100);
    Serial.print("[WIFI] AP \"");
    Serial.print(WIFI_SSID);
    Serial.print("\" no IP ");
    Serial.println(WiFi.softAPIP());
}

void setupSta() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("[WIFI] STA conectando a \"");
    Serial.print(WIFI_SSID);
    Serial.print("\"");
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 40) {
        delay(250);
        Serial.print(".");
        ++tries;
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("[WIFI] STA conectada, IP ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("[WIFI] STA falhou — caindo pra AP");
        setupAp();
    }
}

void task(void*) {
    WifiBridge::mutex = xSemaphoreCreateMutex();

#if WIFI_USE_AP
    setupAp();
#else
    setupSta();
#endif

    s_server.begin();
    s_server.setNoDelay(true);
    Serial.print("[WIFI] TCP server escutando porta ");
    Serial.println(WIFI_TCP_PORT);

    for (;;) {
        // 1) Aceita cliente novo (só permite um por vez — derruba o anterior).
        if (s_server.hasClient()) {
            if (WifiBridge::client && WifiBridge::client.connected()) {
                Serial.println("[WIFI] Cliente novo chegou — derrubando o anterior");
                WifiBridge::client.stop();
            }
            WifiBridge::client = s_server.available();
            WifiBridge::client.setNoDelay(true);
            Serial.print("[WIFI] Cliente conectado: ");
            Serial.println(WifiBridge::client.remoteIP());
        }

        // 2) Drena bytes recebidos do cliente → parser MAVLink
        if (WifiBridge::client && WifiBridge::client.connected()) {
            while (WifiBridge::client.available()) {
                uint8_t c = (uint8_t)WifiBridge::client.read();
                MavlinkRxTask::processByte(c, 2);  // MAVLINK_COMM_2
            }
        } else if (WifiBridge::client) {
            // Limpa handle quando o cliente desconectou
            Serial.println("[WIFI] Cliente desconectou");
            WifiBridge::client.stop();
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

}  // anon

void start() {
    xTaskCreatePinnedToCore(task, "WifiTask", 6144, nullptr, 2, nullptr, 0);
}

}  // namespace WifiTask

#else  // WIFI_BRIDGE_ENABLE == 0 — task vazio

namespace WifiTask { void start() {} }

#endif
