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
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <TinyGPSPlus.h>
#include "crsf_rx.h"
#include "mavlink_min.h"

// =============================================================================
// MAVLINK_ENABLED: 1 = Serial USB fala MAVLink (Mission Planner / QGC enxergam
// como vehicle); 0 = Serial USB fala texto humano (debug clássico). Não dá pra
// fazer os dois ao mesmo tempo: bytes MAVLink no meio do texto bagunçam o GCS,
// e prints de debug no meio do MAVLink quebram o protocolo.
// =============================================================================
#define MAVLINK_ENABLED  1   // 0 = debug texto p/ validar link CRSF; 1 = MAVLink p/ Mission Planner

// =============================================================================
// PINAGEM
// =============================================================================
// Saídas PWM
// CH1 (ESC/Throttle) movido de GPIO12 -> GPIO4: GPIO12 é strapping pin e falhava
// no attach do ESP32Servo. GPIO4 é livre (era nRF24 CE) e seguro p/ sinal de ESC.
constexpr uint8_t PIN_CH1 = 4;    // Throttle / ESC
constexpr uint8_t PIN_CH2 = 13;   // Rudder (não usado em delta puro)
constexpr uint8_t PIN_CH3 = 14;   // Elevon ESQUERDO (saída pós-mix)
constexpr uint8_t PIN_CH4 = 27;   // Elevon DIREITO  (saída pós-mix)
constexpr uint8_t PIN_CH5 = 26;   // Aux1
constexpr uint8_t PIN_CH6 = 25;   // Aux2
constexpr uint8_t PIN_CH7 = 16;   // Aux3
constexpr uint8_t PIN_CH8 = 17;   // Aux4

// CRSF UART (UART2 do ESP32, remapeada para pinos livres)
constexpr uint8_t PIN_CRSF_RX = 32;   // ← TX do RX ELRS
constexpr uint8_t PIN_CRSF_TX = 33;   // → RX do RX ELRS (telemetria, futuro)

// LED debug (LED_BUILTIN varia por placa; em DevKit comum é 2)
constexpr uint8_t PIN_LED = 2;

// MPU6050 (I2C) — atitude p/ horizonte artificial
constexpr uint8_t PIN_MPU_SDA = 21;
constexpr uint8_t PIN_MPU_SCL = 22;

// GPS (UART1 do ESP32) — GY-NEO6MV2, 9600 baud NMEA
constexpr uint8_t PIN_GPS_RX = 18;    // ← TX do GPS
constexpr uint8_t PIN_GPS_TX = 19;    // → RX do GPS (config, opcional)

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
// Mapeamento AETR (padrão EdgeTX/RadioMaster) — confirmado pelo estado de repouso
constexpr uint8_t CRSF_IDX_AIL  = 0;   // CH1 = Aileron/Roll
constexpr uint8_t CRSF_IDX_ELE  = 1;   // CH2 = Elevator/Pitch
constexpr uint8_t CRSF_IDX_THR  = 2;   // CH3 = Throttle
constexpr uint8_t CRSF_IDX_RUD  = 3;   // CH4 = Rudder/Yaw
constexpr uint8_t CRSF_IDX_ARM  = 4;   // CH5 = Arma/desarma motor (HIGH=armado)
constexpr uint8_t CRSF_IDX_AUX2 = 5;   // CH6 = livre
constexpr uint8_t CRSF_IDX_AUX3 = 6;   // CH7 = livre
constexpr uint8_t CRSF_IDX_MODE = 7;   // CH8 = Modo (HIGH=estabilizado, LOW=manual)

// Limiar p/ tratar canal CRSF como switch (µs). Centro = 1500.
constexpr int SWITCH_THRESHOLD_US = 1500;

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
    bool     armed;       // CH5 HIGH = motor armado (LOW = desarmado/cortado)
    bool     manual_mode; // CH8 LOW = manual; HIGH = estabilizado
};
Inputs in_;

// Estatísticas
uint32_t loop_count = 0;
uint32_t last_print_ms = 0;

// === MPU6050 / AHRS ===
Adafruit_MPU6050 mpu;
bool  mpu_ok = false;
float att_roll_deg = 0.0f, att_pitch_deg = 0.0f;             // ângulos (graus)
float rate_roll = 0.0f, rate_pitch = 0.0f, rate_yaw = 0.0f;  // rad/s (p/ ATTITUDE)
float gyro_bias_x = 0.0f, gyro_bias_y = 0.0f, gyro_bias_z = 0.0f;
uint32_t last_imu_us = 0;
char     g_i2c_report[50] = "I2C: (sem scan)";   // resultado do scan p/ STATUSTEXT
uint8_t  g_mpu_addr = 0;
char     g_pwm_report[50] = "PWM: (sem attach)"; // status do attach das saídas

// === GPS ===
TinyGPSPlus gps;
constexpr float RAD2DEG = 57.2957795f;
constexpr float DEG2RAD = 0.0174532925f;

// Orientação do MPU — inverta os sinais se o horizonte vier trocado/invertido
constexpr float ROLL_SIGN  = 1.0f;
constexpr float PITCH_SIGN = 1.0f;

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
    servoCh1.writeMicroseconds(in_.armed ? in_.thr_us : PWM_IDLE);
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
// MPU6050 / AHRS — filtro complementar (roll/pitch)
// =============================================================================
void calibrateGyro() {
    if (!mpu_ok) return;
    float sx = 0, sy = 0, sz = 0;
    const int N = 300;
    sensors_event_t a, g, t;
    for (int i = 0; i < N; i++) {
        mpu.getEvent(&a, &g, &t);
        sx += g.gyro.x; sy += g.gyro.y; sz += g.gyro.z;
        delay(3);
    }
    gyro_bias_x = sx / N; gyro_bias_y = sy / N; gyro_bias_z = sz / N;
}

void updateIMU() {
    if (!mpu_ok) return;
    sensors_event_t a, g, t;
    mpu.getEvent(&a, &g, &t);

    uint32_t now_us = micros();
    float dt = (last_imu_us == 0) ? 0.01f : (now_us - last_imu_us) / 1000000.0f;
    last_imu_us = now_us;
    if (dt <= 0.0f || dt > 0.2f) dt = 0.01f;

    // Leitura crua (accel m/s², gyro rad/s já sem bias)
    float ax0 = a.acceleration.x, ay0 = a.acceleration.y, az = a.acceleration.z;
    float gx0 = g.gyro.x - gyro_bias_x;
    float gy0 = g.gyro.y - gyro_bias_y;

    // Rotação de 90° HORÁRIO no plano do sensor (em torno do Z/yaw): (x,y) -> (y,-x)
    // (se ficar girado pro lado errado, troque para: ax=-ay0, ay=ax0, gx=-gy0, gy=gx0)
    float ax =  ay0, ay = -ax0;
    float gx =  gy0, gy = -gx0;

    // Ângulos absolutos pelo acelerômetro (referência de nível, estável)
    float accel_roll  = atan2f(ay, az) * RAD2DEG;
    float accel_pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * RAD2DEG;

    // Taxas do giroscópio (rad/s)
    rate_roll  = gx;
    rate_pitch = gy;
    rate_yaw   = g.gyro.z - gyro_bias_z;

    // Filtro complementar: gyro (rápido) + accel (sem drift)
    const float ALPHA = 0.98f;
    att_roll_deg  = ALPHA * (att_roll_deg  + rate_roll  * RAD2DEG * dt) + (1.0f - ALPHA) * accel_roll;
    att_pitch_deg = ALPHA * (att_pitch_deg + rate_pitch * RAD2DEG * dt) + (1.0f - ALPHA) * accel_pitch;
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
#if MAVLINK_ENABLED
    // Em modo MAVLink, NÃO imprimimos texto na Serial USB — o GCS espera só
    // bytes binários. Toda info de boot vai pra Serial2 (mesma do CRSF, em
    // baud diferente, então cuidado se quiser ler). Aqui ficamos mudos.
#else
    Serial.println();
    Serial.println(F("==== ESP32 FC v2 — entrada CRSF (ELRS 915) ===="));
#endif

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
    char fails[40] = "";
    for (uint8_t i = 0; i < 8; i++) {
        servos[i]->setPeriodHertz(50);
        // NOTA: attach() retorna o canal LEDC (0..15), não um bool — canal 0 é
        // válido. Use attached() p/ detectar falha real (evita falso "CH1 falhou").
        servos[i]->attach(pins[i], PWM_MIN, PWM_MAX);
        if (!servos[i]->attached()) { char t[6]; snprintf(t, sizeof(t), " CH%d", i + 1);
                      strncat(fails, t, sizeof(fails) - strlen(fails) - 1); }
    }
    if (fails[0] == 0) snprintf(g_pwm_report, sizeof(g_pwm_report), "PWM: 8 canais OK");
    else               snprintf(g_pwm_report, sizeof(g_pwm_report), "PWM FALHOU:%s", fails);

    // Posição inicial segura
    applyFailsafe();
    armESC_safety();

    // CRSF UART2 — remapeada para GPIO 32 (RX) / 33 (TX), 420000 8N1
    Serial2.begin(CRSF_BAUDRATE, SERIAL_8N1, PIN_CRSF_RX, PIN_CRSF_TX);
#if !MAVLINK_ENABLED
    Serial.printf("CRSF iniciado em UART2: RX=GPIO%d TX=GPIO%d @%lu baud\n",
                  PIN_CRSF_RX, PIN_CRSF_TX, (unsigned long)CRSF_BAUDRATE);
#endif

    // GPS na UART1 (GY-NEO6MV2, 9600 8N1)
    Serial1.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);

    // MPU6050 (não bloqueante: se faltar, segue como RX puro, só sem horizonte)
    Wire.begin(PIN_MPU_SDA, PIN_MPU_SCL);
    Wire.setClock(100000);   // 100 kHz é mais tolerante a pull-ups fracos

    // --- Scan I2C: descobre o que realmente está no barramento (21/22) ---
    uint8_t addrs[8]; int found = 0;
    for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) { if (found < 8) addrs[found] = a; found++; }
    }
    if (found == 0) {
        snprintf(g_i2c_report, sizeof(g_i2c_report), "I2C(21/22): nenhum dispositivo");
    } else {
        snprintf(g_i2c_report, sizeof(g_i2c_report), "I2C(21/22):");
        for (int k = 0; k < found && k < 6; k++) {
            char hx[8]; snprintf(hx, sizeof(hx), " 0x%02X", addrs[k]);
            strncat(g_i2c_report, hx, sizeof(g_i2c_report) - strlen(g_i2c_report) - 1);
        }
    }

    // Tenta MPU6050 em 0x68 e depois 0x69 (AD0 alto)
    if (mpu.begin(0x68))      { mpu_ok = true; g_mpu_addr = 0x68; }
    else if (mpu.begin(0x69)) { mpu_ok = true; g_mpu_addr = 0x69; }
    if (mpu_ok) {
        mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
        mpu.setGyroRange(MPU6050_RANGE_500_DEG);
        mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
        calibrateGyro();   // ~0.9 s — mantenha a placa parada no boot
    }
#if !MAVLINK_ENABLED
    Serial.printf("%s | MPU6050: %s (0x%02X)\n", g_i2c_report, mpu_ok ? "OK" : "NAO", g_mpu_addr);
#endif

    // Inputs iniciais em estado seguro: desarmado + manual
    in_ = {PWM_IDLE, 0.0f, 0.0f, 1500, 1500, 1500, 1500, false, true};

#if !MAVLINK_ENABLED
    Serial.println(F("Sistema pronto. Aguardando CRSF..."));
#endif
}

// =============================================================================
// LOOP PRINCIPAL
// =============================================================================
void loop() {
    // 1. Alimenta o parser com tudo que tiver na UART
    while (Serial2.available()) {
        crsf.feed((uint8_t)Serial2.read());
    }

    // 1b. Alimenta o parser de GPS com o NMEA da UART1
    while (Serial1.available()) {
        gps.encode((char)Serial1.read());
    }

    // 2. Decodifica canais quando frame novo chegou
    if (crsf.hasNewFrame()) {
        crsf.clearFrameFlag();

        // Mapeia canais CRSF -> estrutura semântica
        in_.thr_us   = crsf_channel_to_us(crsf.getChannel(CRSF_IDX_THR));
        in_.rud_us   = crsf_channel_to_us(crsf.getChannel(CRSF_IDX_RUD));
        in_.aux1_us  = crsf_channel_to_us(crsf.getChannel(CRSF_IDX_ARM));
        in_.aux2_us  = crsf_channel_to_us(crsf.getChannel(CRSF_IDX_AUX2));
        in_.aux3_us  = crsf_channel_to_us(crsf.getChannel(CRSF_IDX_AUX3));

        // Sticks normalizados para mixagem elevon
        in_.ele_norm = crsf_to_norm(crsf.getChannel(CRSF_IDX_ELE));
        in_.ail_norm = crsf_to_norm(crsf.getChannel(CRSF_IDX_AIL));

        // Switches: CH5 = arm (HIGH=armado), CH8 = modo (LOW=manual, HIGH=estab)
        in_.armed       = crsf_channel_to_us(crsf.getChannel(CRSF_IDX_ARM))  > SWITCH_THRESHOLD_US;
        in_.manual_mode = crsf_channel_to_us(crsf.getChannel(CRSF_IDX_MODE)) < SWITCH_THRESHOLD_US;

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

    // 5. Saída periódica na Serial USB (MAVLink ou texto, conforme MAVLINK_ENABLED)
    uint32_t now = millis();

    // IMU: atualiza atitude a ~100 Hz (filtro complementar)
    static uint32_t last_imu_ms = 0;
    if (mpu_ok && now - last_imu_ms >= 10) {
        last_imu_ms = now;
        updateIMU();
    }

#if MAVLINK_ENABLED
    // ---- HEARTBEAT a 1 Hz (Mission Planner reconhece o vehicle) ----
    static uint32_t last_heartbeat_ms = 0;
    if (now - last_heartbeat_ms >= 1000) {
        last_heartbeat_ms = now;
        // base_mode: manual input habilitado SE não está em failsafe
        uint8_t base = (millis() - crsf.lastFrameMs() > RX_TIMEOUT_MS)
                       ? 0
                       : MAV_MODE_FLAG_MANUAL_INPUT_ENABLED;
        uint8_t state = (millis() - crsf.lastFrameMs() > RX_TIMEOUT_MS)
                        ? MAV_STATE_STANDBY
                        : MAV_STATE_ACTIVE;
        mav_send_heartbeat(Serial, base, state);
    }

    // ---- RC_CHANNELS_RAW a 10 Hz (canais aparecem no painel "Radio") ----
    static uint32_t last_rc_ms = 0;
    if (now - last_rc_ms >= 100) {
        last_rc_ms = now;
        // Canais na ordem NATIVA do rádio (AETR) — assim o painel "Radio" do
        // Mission Planner mostra Roll/Pitch/Throttle/Yaw nas barras certas.
        uint16_t ch[8] = {
            (uint16_t)crsf_channel_to_us(crsf.getChannel(0)),  // CH1 Roll/Aileron
            (uint16_t)crsf_channel_to_us(crsf.getChannel(1)),  // CH2 Pitch/Elevator
            (uint16_t)crsf_channel_to_us(crsf.getChannel(2)),  // CH3 Throttle
            (uint16_t)crsf_channel_to_us(crsf.getChannel(3)),  // CH4 Yaw/Rudder
            (uint16_t)crsf_channel_to_us(crsf.getChannel(4)),  // CH5 Arm
            (uint16_t)crsf_channel_to_us(crsf.getChannel(5)),  // CH6
            (uint16_t)crsf_channel_to_us(crsf.getChannel(6)),  // CH7
            (uint16_t)crsf_channel_to_us(crsf.getChannel(7))   // CH8 Modo
        };
        mav_send_rc_channels_raw(Serial, now, ch);
    }

    // ---- ATTITUDE a 25 Hz (horizonte artificial no MP) ----
    static uint32_t last_att_ms = 0;
    if (now - last_att_ms >= 40) {
        last_att_ms = now;
        mav_send_attitude(Serial, now,
                          ROLL_SIGN  * att_roll_deg  * DEG2RAD,
                          PITCH_SIGN * att_pitch_deg * DEG2RAD,
                          0.0f,
                          ROLL_SIGN * rate_roll, PITCH_SIGN * rate_pitch, rate_yaw);
    }

    // ---- GPS a 5 Hz: GPS_RAW_INT (status/sats) + GLOBAL_POSITION_INT (mapa) ----
    static uint32_t last_gps_ms = 0;
    if (now - last_gps_ms >= 200) {
        last_gps_ms = now;
        bool     fixv = gps.location.isValid();
        uint8_t  sats = gps.satellites.isValid() ? (uint8_t)gps.satellites.value() : 0;
        uint8_t  fix  = fixv ? 3 : 0;                                  // 3 = 3D (aprox)
        int32_t  lat  = fixv ? (int32_t)(gps.location.lat() * 1e7) : 0;
        int32_t  lon  = fixv ? (int32_t)(gps.location.lng() * 1e7) : 0;
        int32_t  alt  = gps.altitude.isValid() ? (int32_t)(gps.altitude.meters() * 1000.0) : 0;
        uint16_t vel  = gps.speed.isValid()    ? (uint16_t)(gps.speed.mps() * 100.0)   : 0;
        uint16_t cog  = gps.course.isValid()   ? (uint16_t)(gps.course.deg() * 100.0)  : 0;
        mav_send_gps_raw_int(Serial, (uint64_t)now * 1000ULL, fix, lat, lon, alt, vel, cog, sats);
        if (fixv) {  // só põe ícone no mapa quando tem fix (evita 0,0)
            uint16_t hdg = gps.course.isValid() ? (uint16_t)(gps.course.deg() * 100.0) : 65535;
            mav_send_global_position_int(Serial, now, lat, lon, alt, alt, 0, 0, 0, hdg);
        }
    }

    // ---- STATUSTEXT a cada 3 s, rotativo: I2C/MPU, PWM, GPS (aba Messages) ----
    static uint32_t last_status_ms = 0;
    static uint8_t  status_idx = 0;
    if (now - last_status_ms >= 3000) {
        last_status_ms = now;
        char gpsbuf[50];
        const char *msg; uint8_t sev = MAV_SEVERITY_INFO;
        switch (status_idx++ % 3) {
            case 0: msg = g_i2c_report; sev = mpu_ok ? MAV_SEVERITY_INFO : MAV_SEVERITY_CRITICAL; break;
            case 1: msg = g_pwm_report; sev = strstr(g_pwm_report, "FALH") ? MAV_SEVERITY_CRITICAL : MAV_SEVERITY_INFO; break;
            default:
                snprintf(gpsbuf, sizeof(gpsbuf), "GPS: %s sats=%u",
                         gps.location.isValid() ? "FIX" : "sem fix",
                         gps.satellites.isValid() ? (unsigned)gps.satellites.value() : 0);
                msg = gpsbuf; break;
        }
        mav_send_statustext(Serial, sev, msg);
    }
#else
    // ---- Debug texto (4 Hz) — linha semântica (mapa AETR confirmado) ----
    if (now - last_print_ms >= 250) {
        last_print_ms = now;
        Serial.printf("thr=%d ail=%+.2f ele=%+.2f rud=%d | %s | %s | ok=%lu bad=%lu\n",
                      in_.thr_us, in_.ail_norm, in_.ele_norm, in_.rud_us,
                      in_.armed       ? "ARMADO  " : "DESARMADO",
                      in_.manual_mode ? "MANUAL" : "ESTAB ",
                      (unsigned long)crsf.goodFrames(),
                      (unsigned long)crsf.badFrames());
    }
#endif

    loop_count++;
}
