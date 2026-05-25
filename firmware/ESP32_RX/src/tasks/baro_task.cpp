#include "baro_task.h"
#include "../config.h"
#include "../flight_data.h"

#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ============================================================================
// baroTask — PLACEHOLDER
//
// Quando você instalar um BMP280 (ou similar):
//   1. Adicione `adafruit/Adafruit BMP280 Library` em platformio.ini
//   2. Descomente o bloco BMP280 abaixo
//   3. Remova o placeholder
//
// O Wire (I2C) já é inicializado por imuTask. Bus compartilhado com o MPU6050.
// ============================================================================

// #include <Adafruit_BMP280.h>
// static Adafruit_BMP280 s_baro;
// static float s_p0_hpa = 1013.25f;   // pressão de referência (será setada no boot)

namespace BaroTask {
namespace {

void task(void*) {
    Serial.println("[BARO] PLACEHOLDER ativo (sem sensor instalado)");

    // ----- Quando habilitar BMP280 real, substitua por: -----
    // if (!s_baro.begin(0x76)) {
    //     Serial.println("[BARO] ERRO: BMP280 nao detectado em 0x76, tentando 0x77");
    //     if (!s_baro.begin(0x77)) {
    //         Serial.println("[BARO] ERRO FATAL: BMP280 ausente");
    //         vTaskDelete(NULL);
    //         return;
    //     }
    // }
    // s_baro.setSampling(Adafruit_BMP280::MODE_NORMAL,
    //                    Adafruit_BMP280::SAMPLING_X2,
    //                    Adafruit_BMP280::SAMPLING_X16,
    //                    Adafruit_BMP280::FILTER_X16,
    //                    Adafruit_BMP280::STANDBY_MS_500);
    // // Define ground level: média de 50 amostras
    // double sum = 0;
    // for (int i = 0; i < 50; ++i) { sum += s_baro.readPressure(); vTaskDelay(pdMS_TO_TICKS(20)); }
    // s_p0_hpa = (float)(sum / 50.0 / 100.0);

    const TickType_t period = pdMS_TO_TICKS(50);  // 20 Hz
    TickType_t lastWake = xTaskGetTickCount();

    for (;;) {
        // ----- Substitua por leitura real: -----
        // float pressure_pa = s_baro.readPressure();
        // float temp_c = s_baro.readTemperature();
        // float alt_m  = s_baro.readAltitude(s_p0_hpa);

        FlightData::lock();
        g_fd.baro_press_pa = 0.0f;
        g_fd.baro_temp_c   = 0.0f;
        g_fd.baro_alt_m    = 0.0f;
        FlightData::unlock();

        vTaskDelayUntil(&lastWake, period);
    }
}

}  // anon

void start() {
    xTaskCreatePinnedToCore(task, "BaroTask", 3072, nullptr, 2, nullptr, 0);
}

}  // namespace BaroTask
