#include "radio_task.h"
#include "../config.h"
#include "../flight_data.h"

#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace RadioTask {
namespace {

const byte RADIO_ADDR[6] = "00001";

struct __attribute__((packed)) PacketRF {
    uint8_t p[8];
    uint8_t s1;
    uint8_t s2;
} s_pkt;

RF24 s_radio(PIN_NRF_CE, PIN_NRF_CSN);
uint32_t s_last_pkt_ms = 0;

// Calibração RC + trims persistidos em NVS (mantém o que já funcionava)
int   s_rc_min[8], s_rc_max[8], s_rc_center[8], s_rc_trim[8];
int   s_servo_trim_l = 0, s_servo_trim_r = 0;
float s_deadzone = 0.05f;

inline float byteToNorm(uint8_t b) { return ((int)b - 128) / 127.0f; }
inline float applyExpo(float v, float e) {
    return v * (1.0f - e) + v * v * v * e;
}
inline float applyDeadzone(float v, float dz) {
    if (dz <= 0.0f) return v;
    if (dz >= 0.5f) return 0.0f;
    if (fabsf(v) <= dz) return 0.0f;
    float sign = (v > 0) ? 1.0f : -1.0f;
    return sign * (fabsf(v) - dz) / (1.0f - dz);
}

void loadCalibration() {
    Preferences prefs;
    prefs.begin("fc-rc", true);
    char key[16];
    for (int i = 0; i < 8; ++i) {
        snprintf(key, sizeof(key), "rcmin%d", i);
        s_rc_min[i] = prefs.getInt(key, PWM_MAX);
        snprintf(key, sizeof(key), "rcmax%d", i);
        s_rc_max[i] = prefs.getInt(key, PWM_MIN);
        snprintf(key, sizeof(key), "rccenter%d", i);
        s_rc_center[i] = prefs.getInt(key, 1500);
        snprintf(key, sizeof(key), "rctrim%d", i);
        s_rc_trim[i] = prefs.getInt(key, 0);
    }
    s_servo_trim_l = prefs.getInt("servo_trim_l", 0);
    s_servo_trim_r = prefs.getInt("servo_trim_r", 0);
    s_deadzone     = prefs.getFloat("deadzone", 0.05f);
    prefs.end();
}

void task(void*) {
    SPI.begin();
    if (!s_radio.begin()) {
        Serial.println("[RADIO] ERRO FATAL: nRF24L01 nao detectado");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    s_radio.setPALevel(RF24_PA_LOW);
    s_radio.setDataRate(RF24_250KBPS);
    s_radio.setChannel(76);
    s_radio.setAutoAck(false);
    s_radio.openReadingPipe(0, RADIO_ADDR);
    s_radio.startListening();
    s_last_pkt_ms = millis();
    Serial.println("[RADIO] Listening on ch 76");

    static uint32_t s_rx_count = 0;
    static uint32_t s_last_print = 0;
    for (;;) {
        if (s_radio.available()) {
            s_radio.read(&s_pkt, sizeof(s_pkt));
            s_last_pkt_ms = millis();
            s_rx_count++;

            int thr = constrain(
                map(s_pkt.p[CH_THR], 0, 255, PWM_MIN, PWM_MAX),
                PWM_MIN, PWM_MAX);

            float e = applyDeadzone(byteToNorm(s_pkt.p[CH_ELE]), s_deadzone);
            float a = applyDeadzone(byteToNorm(s_pkt.p[CH_AIL]), s_deadzone);
#if REVERSE_ELE
            e = -e;
#endif
#if REVERSE_AIL
            a = -a;
#endif
            float pitch_cmd = applyExpo(e, EXPO_E);
            float roll_cmd  = applyExpo(a, EXPO_A);

            FlightData::lock();
            g_fd.throttle_us     = thr;
            g_fd.cmd_pitch       = pitch_cmd;
            g_fd.cmd_roll        = roll_cmd;
            g_fd.manual_mode     = (s_pkt.s1 == 1);
            g_fd.failsafe_active = false;
            // SW2 reservado pra arm/disarm físico futuramente
            g_fd.armed = (s_pkt.s2 == 1);
            FlightData::unlock();
        }

        if (millis() - s_last_pkt_ms > RX_TIMEOUT_MS) {
            FlightData::lock();
            g_fd.failsafe_active = true;
            FlightData::unlock();
        }

        // Debug: imprime taxa de pacotes RX a cada 1s
        if (millis() - s_last_print > 1000) {
            Serial.print("[RADIO] pkts/s = ");
            Serial.print(s_rx_count);
            Serial.print(" thr=");
            Serial.print(s_pkt.p[CH_THR]);
            Serial.print(" ele=");
            Serial.print(s_pkt.p[CH_ELE]);
            Serial.print(" ail=");
            Serial.println(s_pkt.p[CH_AIL]);
            s_rx_count = 0;
            s_last_print = millis();
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

}  // anon

int trimL() { return s_rc_trim[CH_AIL] + s_servo_trim_l; }
int trimR() { return s_rc_trim[CH_AIL] + s_servo_trim_r; }

void start() {
    loadCalibration();
    xTaskCreatePinnedToCore(task, "RadioTask", 4096, nullptr, 4, nullptr, 0);
}

}  // namespace RadioTask
