#include "imu_task.h"
#include "../config.h"
#include "../flight_data.h"

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace ImuTask {
namespace {

Adafruit_MPU6050 s_mpu;
volatile bool s_cal_request = false;

double s_offset_roll  = 0.0;
double s_offset_pitch = 0.0;
double s_offset_yaw   = 0.0;

float att_roll  = 0.0f;
float att_pitch = 0.0f;
float att_yaw   = 0.0f;      // integrado do giro — deriva sem magnetômetro
uint64_t last_us = 0;

// Aplica swap/invert nos eixos. Retorna os 3 valores remapeados (gx_out etc).
inline void remap(float xin, float yin, float zin,
                  float& xout, float& yout, float& zout) {
#if IMU_SWAP_XY
    float a = yin, b = xin;
#else
    float a = xin, b = yin;
#endif
#if IMU_INVERT_X
    a = -a;
#endif
#if IMU_INVERT_Y
    b = -b;
#endif
    float c = zin;
#if IMU_INVERT_Z
    c = -c;
#endif
    xout = a; yout = b; zout = c;
}

void calibrate() {
    Serial.println("[IMU] Calibrating gyro... (mantenha parado)");
    sensors_event_t a, g, t;
    double sx = 0, sy = 0, sz = 0;
    for (int i = 0; i < 1000; ++i) {
        s_mpu.getEvent(&a, &g, &t);
        float gx, gy, gz;
        remap(g.gyro.x, g.gyro.y, g.gyro.z, gx, gy, gz);
        sx += gx;
        sy += gy;
        sz += gz;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    s_offset_roll  = (sx / 1000.0) * RAD2DEG;
    s_offset_pitch = (sy / 1000.0) * RAD2DEG;
    s_offset_yaw   = (sz / 1000.0) * RAD2DEG;
    Serial.println("[IMU] Gyro calibrado.");
}

void task(void*) {
    // Wire.begin() já foi feito no setup() do main.cpp (I2C compartilhado com baro)
    // Tenta endereço padrão 0x68, depois 0x69 (AD0 em HIGH)
    if (!s_mpu.begin(0x68) && !s_mpu.begin(0x69)) {
        Serial.println("[IMU] ERRO FATAL: MPU6050 nao detectado em 0x68 nem 0x69");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    s_mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    s_mpu.setGyroRange(MPU6050_RANGE_500_DEG);

    calibrate();

    // Seed do filtro complementar com leitura do acelerômetro
    sensors_event_t a, g, t;
    s_mpu.getEvent(&a, &g, &t);
    float ax0, ay0, az0;
    remap(a.acceleration.x, a.acceleration.y, a.acceleration.z, ax0, ay0, az0);
    att_roll  = atan2f(ay0, az0) * RAD2DEG;
    att_pitch = atan2f(-ax0, sqrtf(ay0 * ay0 + az0 * az0)) * RAD2DEG;
    att_yaw   = 0.0f;
    last_us   = esp_timer_get_time();

    const TickType_t period = pdMS_TO_TICKS(1000 / LOOP_FREQ_HZ);
    TickType_t lastWake = xTaskGetTickCount();

    for (;;) {
        if (s_cal_request) {
            s_cal_request = false;
            calibrate();
            last_us = esp_timer_get_time();
        }

        s_mpu.getEvent(&a, &g, &t);

        float gx, gy, gz;
        remap(g.gyro.x, g.gyro.y, g.gyro.z, gx, gy, gz);
        float ax, ay, az;
        remap(a.acceleration.x, a.acceleration.y, a.acceleration.z, ax, ay, az);

        float rr = (gx * RAD2DEG) - s_offset_roll;
        float pr = (gy * RAD2DEG) - s_offset_pitch;
        float yr = (gz * RAD2DEG) - s_offset_yaw;

        uint64_t now_us = esp_timer_get_time();
        float dt = (now_us - last_us) / 1e6f;
        last_us = now_us;

        // Integração do giro
        att_roll  += rr * dt;
        att_pitch += pr * dt;
        att_yaw   += yr * dt;

        // Wrap yaw em [-180, 180]
        if (att_yaw >  180.0f) att_yaw -= 360.0f;
        if (att_yaw < -180.0f) att_yaw += 360.0f;

        // Correção do filtro complementar (apenas roll/pitch — sem mag não tem como corrigir yaw)
        float acc_roll  = atan2f(ay, az) * RAD2DEG;
        float acc_pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * RAD2DEG;
        att_roll  = AHRS_ALPHA * att_roll  + (1.0f - AHRS_ALPHA) * acc_roll;
        att_pitch = AHRS_ALPHA * att_pitch + (1.0f - AHRS_ALPHA) * acc_pitch;

        FlightData::lock();
        g_fd.roll_deg        = att_roll;
        g_fd.pitch_deg       = att_pitch;
        g_fd.yaw_deg         = att_yaw;
        g_fd.roll_rate_dps   = pr;   // pr = −gyro.x (eixo transversal = roll físico)
        g_fd.pitch_rate_dps  = rr;   // rr = −gyro.y (eixo longitudinal = pitch físico)
        g_fd.yaw_rate_dps    = yr;
        FlightData::unlock();

        vTaskDelayUntil(&lastWake, period);
    }
}

}  // anon

void start() {
    xTaskCreatePinnedToCore(task, "ImuTask", 4096, nullptr, 3, nullptr, 1);
}

void requestGyroCalibration() {
    s_cal_request = true;
}

}  // namespace ImuTask
