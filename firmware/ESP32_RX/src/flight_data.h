#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// =============================================================================
// FlightData — struct global compartilhada entre tasks.
//
// Toda leitura/escrita deve ser feita com FlightData::lock() / unlock(),
// EXCETO os flags volatile no final (set/clear atômicos de bool é seguro).
//
// Cada task escreve só os campos da sua responsabilidade; o osdTask lê tudo
// e empacota em MAVLink.
// =============================================================================
struct FlightData {
    // --- Atitude (atualizado por imuTask) ---
    float roll_deg  = 0.0f;   // graus, do filtro complementar
    float pitch_deg = 0.0f;
    float yaw_deg   = 0.0f;   // graus, integrado do giro (deriva — sem mag)
    float roll_rate_dps  = 0.0f;
    float pitch_rate_dps = 0.0f;
    float yaw_rate_dps   = 0.0f;

    // --- GPS (atualizado por gpsTask) ---
    bool   gps_fix       = false;
    uint8_t gps_sats     = 0;
    double gps_lat       = 0.0;     // graus
    double gps_lon       = 0.0;     // graus
    float  gps_alt_m     = 0.0f;    // metros MSL
    float  gps_speed_mps = 0.0f;
    float  gps_cog_deg   = 0.0f;    // course over ground
    uint32_t gps_time_ms = 0;       // ms desde último fix

    // --- Barômetro (atualizado por baroTask — PLACEHOLDER por enquanto) ---
    float baro_alt_m   = 0.0f;
    float baro_press_pa = 0.0f;
    float baro_temp_c   = 0.0f;

    // --- Bateria (placeholder até ter divisor de tensão) ---
    float batt_voltage_v   = 0.0f;
    float batt_current_a   = 0.0f;
    int8_t batt_remaining_pct = -1;  // -1 = desconhecido

    // --- Comandos do RC (escrito por radioTask, lido por controlTask) ---
    float cmd_roll  = 0.0f;     // normalizado -1..1, com expo+deadzone aplicados
    float cmd_pitch = 0.0f;

    // --- Estado de voo (escrito pela radioTask / controlTask) ---
    bool failsafe_active = true;
    bool manual_mode     = false;
    bool armed           = false;
    int  throttle_us     = 1000;

    // --- Comandos recebidos via MAVLink RX (escrito por mavlinkRxTask) ---
    bool gcs_arm_request    = false;
    bool gcs_disarm_request = false;

    static SemaphoreHandle_t mutex;

    static void init() {
        mutex = xSemaphoreCreateMutex();
    }
    static bool lock(TickType_t timeout = portMAX_DELAY) {
        return xSemaphoreTake(mutex, timeout) == pdTRUE;
    }
    static void unlock() {
        xSemaphoreGive(mutex);
    }
};

extern FlightData g_fd;
