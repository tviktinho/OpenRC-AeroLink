#include "gps_task.h"
#include "../config.h"
#include "../flight_data.h"

#include <Arduino.h>
#include <TinyGPSPlus.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace GpsTask {
namespace {

TinyGPSPlus s_gps;

void task(void*) {
    Serial1.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    Serial.println("[GPS] Serial1 iniciada");

    for (;;) {
        // Drena tudo que veio no buffer e empurra pro parser NMEA
        while (Serial1.available()) {
            char c = Serial1.read();
            if (s_gps.encode(c) && s_gps.location.isValid()) {
                FlightData::lock();
                g_fd.gps_fix       = true;
                g_fd.gps_lat       = s_gps.location.lat();
                g_fd.gps_lon       = s_gps.location.lng();
                g_fd.gps_alt_m     = s_gps.altitude.isValid() ? s_gps.altitude.meters() : 0.0f;
                g_fd.gps_speed_mps = s_gps.speed.isValid()    ? s_gps.speed.mps()       : 0.0f;
                g_fd.gps_cog_deg   = s_gps.course.isValid()   ? s_gps.course.deg()      : 0.0f;
                g_fd.gps_sats      = s_gps.satellites.isValid() ? s_gps.satellites.value() : 0;
                g_fd.gps_time_ms   = millis();
                FlightData::unlock();
            }
        }

        // Marca perda de fix se ficar 3s sem update
        FlightData::lock();
        if (g_fd.gps_fix && (millis() - g_fd.gps_time_ms > 3000)) {
            g_fd.gps_fix = false;
        }
        FlightData::unlock();

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

}  // anon

void start() {
    xTaskCreatePinnedToCore(task, "GpsTask", 3072, nullptr, 2, nullptr, 0);
}

}  // namespace GpsTask
