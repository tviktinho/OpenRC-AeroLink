#include "osd_task.h"
#include "../config.h"
#include "../flight_data.h"
#include "../mavlink_helper.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ============================================================================
// osdTask — envia stream MAVLink @ UART2 (compartilhado MinimOSD + rádio SiK).
//
// Schedule por mensagem (sem mandar tudo no mesmo tick — distribui carga
// da UART e dá folga pro buffer):
//
//   HEARTBEAT             1 Hz   (anuncia FC, dispara render na OSD)
//   ATTITUDE             10 Hz   (alimenta horizonte virtual)
//   GPS_RAW_INT           5 Hz   (lat/lon/satélites)
//   VFR_HUD               5 Hz   (heading, GS, altitude)
//   SYS_STATUS            2 Hz   (bateria, saúde)
//   GLOBAL_POSITION_INT   5 Hz   (posição fundida)
//
// Loop a 50 Hz (20 ms). Verificamos cada timestamp individualmente.
// ============================================================================

namespace OsdTask {
namespace {

void task(void*) {
    // Mav::begin() já foi chamado no setup() do main.cpp (Serial2 + flag v1)
    Serial.print("[OSD] MAVLink TX @ ");
    Serial.println(MAV_BAUD);

    // Offsets iniciais espalhados pra não acumular num mesmo loop
    uint32_t t_hb   = 0;
    uint32_t t_att  = 20;
    uint32_t t_gps  = 40;
    uint32_t t_vfr  = 60;
    uint32_t t_sys  = 80;
    uint32_t t_gpos = 100;

    const TickType_t period = pdMS_TO_TICKS(20);
    TickType_t lastWake = xTaskGetTickCount();

    for (;;) {
        uint32_t now = millis();

        // HEARTBEAT 1 Hz
        if (now - t_hb >= 1000) {
            Mav::sendHeartbeat();
            t_hb = now;
        }
        // ATTITUDE 10 Hz
        if (now - t_att >= 100) {
            Mav::sendAttitude();
            t_att = now;
        }
        // GPS_RAW_INT 5 Hz
        if (now - t_gps >= 200) {
            Mav::sendGpsRawInt();
            t_gps = now;
        }
        // VFR_HUD 5 Hz
        if (now - t_vfr >= 200) {
            Mav::sendVfrHud();
            t_vfr = now;
        }
        // SYS_STATUS 2 Hz
        if (now - t_sys >= 500) {
            Mav::sendSysStatus();
            t_sys = now;
        }
        // GLOBAL_POSITION_INT 5 Hz
        if (now - t_gpos >= 200) {
            Mav::sendGlobalPositionInt();
            t_gpos = now;
        }

        vTaskDelayUntil(&lastWake, period);
    }
}

}  // anon

void start() {
    xTaskCreatePinnedToCore(task, "OsdTask", 4096, nullptr, 2, nullptr, 0);
}

}  // namespace OsdTask
