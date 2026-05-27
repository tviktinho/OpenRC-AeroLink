/* =============================================================================
 * ESP32 Flight Controller v2 — OpenRC AeroLink (upgrade ELRS, Fase 1.5)
 *
 * Substitui o RX nRF24L01 do firmware/ESP32/ESP32.ino por uma entrada CRSF
 * direta do receptor ELRS 915 MHz (RX 915M / BetaFPV ELRS Lite Nano RX 900).
 *
 * O resto da arquitetura (8 saídas PWM, MPU6050 opcional, mixagem elevon
 * com expo + diferencial + reflex, NVS de trims, failsafe) é mantido idêntico
 * ao firmware original — o usuário do avião não nota diferença na resposta
 * dos servos.
 *
 * MODOS DE VOO (via bit 0 de CH8 = SW1 do transmissor):
 *   SW1 = 0 → MANUAL (elevon puro, sem correção do gyro)
 *   SW1 = 1 → ESTABILIZADO (gyro corrige desvios — TODO Fase 2)
 *
 * MAPEAMENTO CRSF → CANAIS (espelha o NANO_TX_v2):
 *   CH1  = Throttle      (pot A0 do TX)
 *   CH2  = Rudder/Yaw    (pot A1)
 *   CH3  = Aileron/Roll  (pot A2)
 *   CH4  = Elevator/Pitch(pot A3)
 *   CH5  = Aux1          (pot A4)
 *   CH6  = Aux2          (pot A5)
 *   CH7  = Aux3          (pot A6)
 *   CH8  = bitmask dos 4 switches (16 níveis):
 *            bit0 = SW1 (modo manual/estab)
 *            bit1 = SW2 (motor cut)
 *            bit2 = SW3 (livre)
 *            bit3 = SW4 (livre)
 *   CH9..CH16 = reservado (neutro 992)
 *
 * SAÍDAS PWM (mesmos pinos do ESP32.ino legado — 100% compat com fiação atual):
 *   CH1=12, CH2=13, CH3=14, CH4=27, CH5=26, CH6=25, CH7=16, CH8=17
 *
 * ENTRADA CRSF:
 *   UART2 do ESP32 nos pinos GPIO 32 (RX) / GPIO 33 (TX, telemetria opcional).
 *   Pinos 16/17 default da UART2 estão ocupados por CH7/CH8 PWM — por isso
 *   remapeamos via Serial2.begin(...).
 *
 * Failsafe: 200 ms sem frame CRSF -> servos para 1500 µs, motor para 1000 µs.
 * ============================================================================= */

#include <Arduino.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include "crsf_rx.h"

// =============================================================================
// PINAGEM
// =============================================================================
// Saídas PWM (idênticas ao firmware/ESP32/ESP32.ino)
constexpr uint8_t PIN_CH1 = 12;   // Throttle
constexpr uint8_t PIN_CH2 = 13;   // Rudder
constexpr uint8_t PIN_CH3 = 14;   // Aileron (elevon L base)
constexpr uint8_t PIN_CH4 = 27;   // Elevator (elevon R base)
constexpr uint8_t PIN_CH5 = 26;   // Aux1
constexpr uint8_t PIN_CH6 = 25;   // Aux2
constexpr uint8_t PIN_CH7 = 16;   // Aux3
constexpr uint8_t PIN_CH8 = 17;   // Aux4

// CRSF UART (UART2 do ESP32, remapeada para pinos livres)
constexpr uint8_t PIN_CRSF_RX = 32;   // ← TX do RX 915M
constexpr uint8_t PIN_CRSF_TX = 33;   // → RX do RX 915M (telemetria, futuro)

// LED debug (LED_BUILTIN varia por placa; em DevKit comum é 2)
constexpr uint8_t PIN_LED = 2;

// =============================================================================
// CONFIGURAÇÕES DE VOO
// =============================================================================
constexpr int  PWM_MIN  = 1000;
constexpr int  PWM_MAX  = 2000;
constexpr int  PWM_IDLE = 1000;

// Ganhos elevon — mesmos defaults do firmware original
float G_E      = 2.55f;
float G_A      = 2.55f;
float EXPO_E   = 0.25f;
float EXPO_A   = 0.25f;
float DIFF     = -0.30f;
int   REFLEX_US = +30;

// Failsafe
constexpr uint32_t RX_TIMEOUT_MS = 200;

// =============================================================================
// MAPEAMENTO CRSF → ÍNDICES SEMÂNTICOS
// (CRSF é 1-based no usuário, 0-based no array)
// =============================================================================
constexpr uint8_t CRSF_IDX_THR  = 0;   // CH1
constexpr uint8_t CRSF_IDX_RUD  = 1;   // CH2
constexpr uint8_t CRSF_IDX_AIL  = 2;   // CH3
constexpr uint8_t CRSF_IDX_ELE  = 3;   // CH4
constexpr uint8_t CRSF_IDX_AUX1 = 4;   // CH5
constexpr uint8_t CRSF_IDX_AUX2 = 5;   // CH6
constexpr uint8_t CRSF_IDX_AUX3 = 6;   // CH7
constexpr uint8_t CRSF_IDX_SWS  = 7;   // CH8 (bitmask 4 switches)

// =============================================================================
// OBJETOS GLOBAIS
// =============================================================================
CrsfRx crsf;
Servo  servoCh1, servoCh2, servoCh3, servoCh4,
       servoCh5, servoCh6, servoCh7, servoCh8;

// Estado decodificado
struct Inputs {
    int      thr_us;      // 1000..2000 raw (sem mixagem)
    float    ele_norm;    // -1..+1 com expo
    float    ail_norm;    // -1..+1 com expo
    int      rud_us;
    int      aux1_us, aux2_us, aux3_us;
    uint8_t  sw_bits;     // 0..15 (bit0=SW1, bit1=SW2, ...)
    bool     manual_mode; // = bit0 (SW1) de sw_bits
    bool     motor_cut;   // = bit1 (SW2) de sw_bits
};
Inputs in_;

// Estatísticas
uint32_t loop_count = 0;
uint32_t last_print_ms = 0;

// =============================================================================
// HELPERS DE PROCESSAMENTO
// =============================================================================
inline float crsf_to_norm(uint16_t ch) {
    // CRSF (172..1811) -> -1..+1 (centro em 992)
    int delta = (int)ch - (int)CRSF_CHANNEL_VALUE_MID;
    // semi-amplitude = (1811 - 992) = 819
    return (float)delta / 819.0f;
}

inline float apply_expo(float v, float e) {
    return v * (1.0f - e) + v * v * v * e;
}

inline int norm_to_us(float v) {
    if (v < -1.0f) v = -1.0f;
    if (v >  1.0f) v =  1.0f;
    return (int)(1500.0f + v * 400.0f);
}

// =============================================================================
// MIXAGEM ELEVON (idêntica ao firmware/ESP32/ESP32.ino)
// =============================================================================
void mixAndWriteElevon() {
    // Combinação elevon: L = E - A, R = E + A
    float E = apply_expo(in_.ele_norm, EXPO_E) * G_E;
    float A = apply_expo(in_.ail_norm, EXPO_A) * G_A;

    float L = E - A;
    float R = E + A;

    auto withDiff = [&](float x) {
        if (x < 0) x *= (1.0f - DIFF);
        return x;
    };
    L = withDiff(L);
    R = withDiff(R);

    int usL = norm_to_us(L) + REFLEX_US;
    int usR = norm_to_us(R) + REFLEX_US;

    usL = constrain(usL, PWM_MIN, PWM_MAX);
    usR = constrain(usR, PWM_MIN, PWM_MAX);

    // CH3 = aileron base (vira elevon L), CH4 = elevator base (vira elevon R)
    servoCh3.writeMicroseconds(usL);
    servoCh4.writeMicroseconds(usR);
}

void writeAuxiliaries() {
    servoCh1.writeMicroseconds(in_.motor_cut ? PWM_IDLE : in_.thr_us);
    servoCh2.writeMicroseconds(in_.rud_us);
    servoCh5.writeMicroseconds(in_.aux1_us);
    servoCh6.writeMicroseconds(in_.aux2_us);
    servoCh7.writeMicroseconds(in_.aux3_us);
    servoCh8.writeMicroseconds(1500);
}

void applyFailsafe() {
    servoCh1.writeMicroseconds(PWM_IDLE);                  // motor cortado
    servoCh2.writeMicroseconds(1500);                      // rudder neutro
    servoCh3.writeMicroseconds(1500 + REFLEX_US);          // elevon L neutro+reflex
    servoCh4.writeMicroseconds(1500 + REFLEX_US);          // elevon R neutro+reflex
    servoCh5.writeMicroseconds(1500);
    servoCh6.writeMicroseconds(1500);
    servoCh7.writeMicroseconds(1500);
    servoCh8.writeMicroseconds(1500);
}

void armESC_safety() {
    for (int i = 0; i < 100; ++i) {
        servoCh1.writeMicroseconds(PWM_IDLE);
        delay(10);
    }
}

// =============================================================================
// SETUP
// =============================================================================
void setup() {
    // Economia de bateria: desliga WiFi e BT (~80 mA)
    WiFi.mode(WIFI_OFF);
    esp_wifi_stop();
    esp_bt_controller_disable();

    Serial.begin(115200);
    delay(50);
    Serial.println();
    Serial.println(F("==== ESP32 FC v2 — entrada CRSF (ELRS 915) ===="));

    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);

    // Saídas PWM
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);

    Servo*  servos[] = {&servoCh1, &servoCh2, &servoCh3, &servoCh4,
                        &servoCh5, &servoCh6, &servoCh7, &servoCh8};
    uint8_t pins  [] = {PIN_CH1, PIN_CH2, PIN_CH3, PIN_CH4,
                        PIN_CH5, PIN_CH6, PIN_CH7, PIN_CH8};
    for (uint8_t i = 0; i < 8; i++) {
        servos[i]->setPeriodHertz(50);
        int r = servos[i]->attach(pins[i], PWM_MIN, PWM_MAX);
        if (r == 0) Serial.printf("ERRO: CH%d (GPIO %d) attach falhou\n", i + 1, pins[i]);
    }

    // Posição inicial segura
    applyFailsafe();
    armESC_safety();

    // CRSF UART2 — remapeada para GPIO 32 (RX) / 33 (TX), 420000 8N1
    Serial2.begin(CRSF_BAUDRATE, SERIAL_8N1, PIN_CRSF_RX, PIN_CRSF_TX);
    Serial.printf("CRSF iniciado em UART2: RX=GPIO%d TX=GPIO%d @%lu baud\n",
                  PIN_CRSF_RX, PIN_CRSF_TX, (unsigned long)CRSF_BAUDRATE);

    // Inputs iniciais em estado seguro
    in_ = {PWM_IDLE, 0.0f, 0.0f, 1500, 1500, 1500, 1500, 0, false, true};

    Serial.println(F("Sistema pronto. Aguardando CRSF..."));
}

// =============================================================================
// LOOP PRINCIPAL
// =============================================================================
void loop() {
    // 1. Alimenta o parser com tudo que tiver na UART
    while (Serial2.available()) {
        crsf.feed((uint8_t)Serial2.read());
    }

    // 2. Decodifica canais quando frame novo chegou
    if (crsf.hasNewFrame()) {
        crsf.clearFrameFlag();

        // Mapeia canais CRSF -> estrutura semântica
        in_.thr_us   = crsf_channel_to_us(crsf.getChannel(CRSF_IDX_THR));
        in_.rud_us   = crsf_channel_to_us(crsf.getChannel(CRSF_IDX_RUD));
        in_.aux1_us  = crsf_channel_to_us(crsf.getChannel(CRSF_IDX_AUX1));
        in_.aux2_us  = crsf_channel_to_us(crsf.getChannel(CRSF_IDX_AUX2));
        in_.aux3_us  = crsf_channel_to_us(crsf.getChannel(CRSF_IDX_AUX3));

        // Sticks normalizados para mixagem elevon
        in_.ele_norm = crsf_to_norm(crsf.getChannel(CRSF_IDX_ELE));
        in_.ail_norm = crsf_to_norm(crsf.getChannel(CRSF_IDX_AIL));

        // Switches: CH8 → bitmask
        in_.sw_bits     = crsf_ch8_to_switches(crsf.getChannel(CRSF_IDX_SWS));
        in_.manual_mode = (in_.sw_bits & 0x01) != 0;   // SW1
        in_.motor_cut   = (in_.sw_bits & 0x02) != 0;   // SW2

        // LED pisca a cada frame recebido (visual rápido de link)
        digitalWrite(PIN_LED, !digitalRead(PIN_LED));
    }

    // 3. Failsafe: sem frame por RX_TIMEOUT_MS -> tudo neutro
    if (millis() - crsf.lastFrameMs() > RX_TIMEOUT_MS) {
        applyFailsafe();
        // mantém LED apagado em failsafe
        digitalWrite(PIN_LED, LOW);
    } else {
        // 4. Operação normal: mistura elevon + saídas auxiliares
        // (no futuro: aqui entra estabilização com MPU6050 quando manual_mode == false)
        mixAndWriteElevon();
        writeAuxiliaries();
    }

    // 5. Debug periódico (1 Hz) na Serial USB
    uint32_t now = millis();
    if (now - last_print_ms >= 1000) {
        last_print_ms = now;
        Serial.printf("[FC] ok=%lu bad=%lu | thr=%d ele=%+.2f ail=%+.2f rud=%d | sw=0x%X %s%s\n",
                      (unsigned long)crsf.goodFrames(),
                      (unsigned long)crsf.badFrames(),
                      in_.thr_us, in_.ele_norm, in_.ail_norm, in_.rud_us,
                      in_.sw_bits,
                      in_.manual_mode ? "MANUAL " : "STAB ",
                      in_.motor_cut   ? "CUT"     : "");
    }

    loop_count++;
}
