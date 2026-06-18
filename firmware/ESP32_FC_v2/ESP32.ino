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
#include <LittleFS.h>
#include <WebServer.h>
#include <Preferences.h>
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
// CH1 (ESC/Throttle) no GPIO21 (liberado ao mover o SDA do I2C p/ o GPIO23).
// GPIO21 é pino limpo (sem strapping/UART) -> attach do PWM funciona.
// (NÃO usar GPIO3/RX: UART0 bloqueia. GPIO12 era strapping. GPIO4 era o anterior.)
constexpr uint8_t PIN_CH1 = 21;   // Throttle / ESC
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
// SDA movido 21->23 p/ liberar o GPIO21 para o ESC (CH1). SCL fica no 22.
constexpr uint8_t PIN_MPU_SDA = 23;
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
// Reverso de cada elevon (0 = normal, 1 = invertido) — ajustável por parâmetro
float srv_rev_l = 0.0f;
float srv_rev_r = 0.0f;

// === AUTO-NÍVEL (modo ângulo, ativado por CH8/ESTAB) ===
// Stick comanda ÂNGULO-alvo; o MPU corrige (P no erro de ângulo + D na taxa).
// COMECE CONSERVADOR e ajuste em bancada/voo — ganho alto = oscilação.
float MAX_ANGLE_DEG       = 45.0f;   // ângulo comandado com stick cheio
float KP_ROLL             = 0.012f;  // P: erro(graus) -> comando normalizado
float KD_ROLL             = 0.0035f; // D: taxa(graus/s) -> comando (amortecimento)
float KP_PITCH            = 0.012f;
float KD_PITCH            = 0.0035f;
float PITCH_LEVEL_OFFSET  = 0.0f;    // alvo de pitch "nivelado" (graus) p/ asa-voadora
// Sinais da correção — INVERTA no bench-test se corrigir pro lado errado
float STAB_ROLL_SIGN      = +1.0f;
float STAB_PITCH_SIGN     = +1.0f;

// Troca eixos roll<->pitch (comando de roll estava agindo como pitch e vice-versa).
// Vale p/ manual E estabilizado. Param AHRS_SWAP: 0 = normal, 1 = trocado.
float swap_rp = 1.0f;

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

// === LOG de trajeto (LittleFS) + WiFi p/ download ===
File     logFile;
bool     log_ok = false;
char     g_log_path[24]   = "/trk.csv";
char     g_log_report[50] = "LOG: (init)";
char     g_wifi_report[40] = "WiFi off";
WebServer server(80);
bool     wifi_on = false;
const char* AP_SSID = "OpenRC-FC";
const char* AP_PASS = "openrcaerolink";    // >= 8 chars
constexpr uint8_t CRSF_IDX_WIFI  = 9;      // CH10 > 50% (desarmado) liga o WiFi
constexpr uint8_t CRSF_IDX_CALIB = 8;      // CH9 (botão momentâneo) calibra o MPU
constexpr int     WIFI_ON_US     = 1500;   // limiar "acima de 50%" (centro)

// MAVLink por WiFi (TCP) — Mission Planner conecta em 192.168.4.1:5760
WiFiServer  mavServer(5760);
WiFiClient  mavClient;
Stream*     g_mav = (Stream*)&Serial;       // saída MAVLink: USB ou cliente WiFi

// Calibração de gyro pelo CH9 (acumula enquanto segura, finaliza ao soltar)
bool     calib_btn = false, calib_was = false;
uint32_t calib_n = 0;
double   calib_sx = 0, calib_sy = 0, calib_sz = 0;
char     g_calib_report[40] = "MPU: ok";
int      mpu_fail = 0;                       // contador de falhas de leitura I2C
constexpr int PIN_BUZZER = -1;               // -1 = sem buzzer; defina o GPIO p/ ativar
constexpr float RAD2DEG = 57.2957795f;
constexpr float DEG2RAD = 0.0174532925f;

// Orientação do MPU (horizonte) — params AHRS_RLL_SGN / AHRS_PIT_SGN (±1)
float ROLL_SIGN  = 1.0f;
float PITCH_SIGN = 1.0f;
// Rotação do MPU no plano (param AHRS_ROT): 0/1/2/3 = 0°/90°/180°/270° horário.
// Ajuste por software quando girar o MPU na PCB. Default 1 = comportamento atual.
float mpu_rot = 1.0f;

// =============================================================================
// PARÂMETROS — editáveis no Mission Planner (MAVLink), salvos na NVS, ao vivo
// =============================================================================
Preferences prefs;
MavParser   mavrx;

struct Param { const char* name; float* ptr; float vmin, vmax; };
Param g_params[] = {
    {"AHRS_RLL_SGN", &ROLL_SIGN,       -1.0f,  1.0f},   // sentido do MPU (roll, horizonte)
    {"AHRS_PIT_SGN", &PITCH_SIGN,      -1.0f,  1.0f},   // sentido do MPU (pitch, horizonte)
    {"AHRS_SWAP",    &swap_rp,          0.0f,  1.0f},   // troca roll<->pitch (0/1)
    {"AHRS_ROT",     &mpu_rot,          0.0f,  3.0f},   // rotação MPU 0/90/180/270°
    {"STB_RLL_SGN",  &STAB_ROLL_SIGN,  -1.0f,  1.0f},   // sentido da correção (roll)
    {"STB_PIT_SGN",  &STAB_PITCH_SIGN, -1.0f,  1.0f},   // sentido da correção (pitch)
    {"SRV_REV_L",    &srv_rev_l,        0.0f,  1.0f},   // reverso elevon esquerdo (0/1)
    {"SRV_REV_R",    &srv_rev_r,        0.0f,  1.0f},   // reverso elevon direito (0/1)
    {"STB_KP_RLL",   &KP_ROLL,          0.0f,  0.20f},  // PID auto-nível
    {"STB_KD_RLL",   &KD_ROLL,          0.0f,  0.10f},
    {"STB_KP_PIT",   &KP_PITCH,         0.0f,  0.20f},
    {"STB_KD_PIT",   &KD_PITCH,         0.0f,  0.10f},
    {"STB_MAXANG",   &MAX_ANGLE_DEG,    5.0f,  80.0f},  // ângulo máx comandado
    {"MIX_GE",       &G_E,              0.0f,  5.0f},   // mixagem elevon
    {"MIX_GA",       &G_A,              0.0f,  5.0f},
    {"MIX_EXPO_E",   &EXPO_E,           0.0f,  0.90f},
    {"MIX_EXPO_A",   &EXPO_A,           0.0f,  0.90f},
    {"MIX_DIFF",     &DIFF,            -0.90f, 0.90f},
};
const uint16_t NPARAM = sizeof(g_params) / sizeof(g_params[0]);

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
// AUTO-NÍVEL (modo ângulo): stick -> ângulo-alvo, MPU corrige (P-ângulo + D-taxa).
// Saída E (pitch) e A (roll) na MESMA escala normalizada do modo manual, então
// alimenta a mesma mixagem elevon abaixo.
// =============================================================================
void computeStabilized(float &E, float &A) {
    float tgt_roll       = in_.ail_norm * MAX_ANGLE_DEG;                    // graus
    float tgt_pitch      = in_.ele_norm * MAX_ANGLE_DEG + PITCH_LEVEL_OFFSET;
    float err_roll       = tgt_roll  - att_roll_deg;                        // graus
    float err_pitch      = tgt_pitch - att_pitch_deg;
    float rate_roll_dps  = rate_roll  * RAD2DEG;                            // graus/s
    float rate_pitch_dps = rate_pitch * RAD2DEG;
    A = STAB_ROLL_SIGN  * (KP_ROLL  * err_roll  - KD_ROLL  * rate_roll_dps);
    E = STAB_PITCH_SIGN * (KP_PITCH * err_pitch - KD_PITCH * rate_pitch_dps);
}

// =============================================================================
// MIXAGEM ELEVON — manual (passthrough) OU auto-nível (CH8/ESTAB)
// =============================================================================
void mixAndWriteElevon() {
    // Combinação elevon: L = E - A, R = E + A
    float E, A;
    if (!in_.manual_mode && mpu_ok) {
        computeStabilized(E, A);                       // auto-nível (CH8 alto)
    } else {
        E = apply_expo(in_.ele_norm, EXPO_E) * G_E;    // manual (passthrough)
        A = apply_expo(in_.ail_norm, EXPO_A) * G_A;
    }

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

    // Reverso por servo (params SRV_REV_L/R): espelha em torno de 1500 µs
    if (srv_rev_l > 0.5f) usL = 3000 - usL;
    if (srv_rev_r > 0.5f) usR = 3000 - usR;

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
    if (!mpu.getEvent(&a, &g, &t)) {     // leitura I2C falhou: não corrompe a atitude
        if (++mpu_fail > 50) { mpu.begin(g_mpu_addr); mpu_fail = 0; }  // re-init se travar
        return;
    }
    mpu_fail = 0;

    // --- Calibração de gyro pelo CH9 (acumula amostras CRUAS enquanto segurado) ---
    if (calib_btn) {
        calib_sx += g.gyro.x; calib_sy += g.gyro.y; calib_sz += g.gyro.z; calib_n++;
        calib_was = true;
        snprintf(g_calib_report, sizeof(g_calib_report), "Calibrando MPU (%lu)", (unsigned long)calib_n);
    } else if (calib_was) {              // soltou o botão -> finaliza
        if (calib_n >= 30) {
            gyro_bias_x = (float)(calib_sx / calib_n);
            gyro_bias_y = (float)(calib_sy / calib_n);
            gyro_bias_z = (float)(calib_sz / calib_n);
            att_roll_deg = 0; att_pitch_deg = 0;   // assume nivelado ao calibrar
            snprintf(g_calib_report, sizeof(g_calib_report), "MPU calibrado (%lu am)", (unsigned long)calib_n);
            // buzzer (se PIN_BUZZER >= 0): apita ao terminar
        }
        calib_sx = calib_sy = calib_sz = 0; calib_n = 0; calib_was = false;
    }

    uint32_t now_us = micros();
    float dt = (last_imu_us == 0) ? 0.01f : (now_us - last_imu_us) / 1000000.0f;
    last_imu_us = now_us;
    if (dt <= 0.0f || dt > 0.2f) dt = 0.01f;

    // Leitura crua (accel m/s², gyro rad/s já sem bias)
    float ax0 = a.acceleration.x, ay0 = a.acceleration.y, az = a.acceleration.z;
    float gx0 = g.gyro.x - gyro_bias_x;
    float gy0 = g.gyro.y - gyro_bias_y;

    // Rotação no plano do sensor conforme AHRS_ROT (0/90/180/270° horário).
    // Ajustável por parâmetro -> conserta giro do MPU na PCB sem reflash.
    float ax, ay, gx, gy;
    switch ((int)(mpu_rot + 0.5f)) {
        case 1:  ax =  ay0; ay = -ax0; gx =  gy0; gy = -gx0; break;  // 90° CW
        case 2:  ax = -ax0; ay = -ay0; gx = -gx0; gy = -gy0; break;  // 180°
        case 3:  ax = -ay0; ay =  ax0; gx = -gy0; gy =  gx0; break;  // 270° CW
        default: ax =  ax0; ay =  ay0; gx =  gx0; gy =  gy0; break;  // 0°
    }

    float accel_roll  = atan2f(ay, az) * RAD2DEG;
    float accel_pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * RAD2DEG;

    rate_roll  = gx;
    rate_pitch = gy;
    rate_yaw   = g.gyro.z - gyro_bias_z;

    // Filtro complementar puro: gyro (rápido) + accel (referência de nível).
    // SEMPRE corrige pelo accel -> é LIMITADO e se auto-corrige (não gira/deriva).
    // (Robustez contra travamento vem do getEvent()/timeout de I2C, não de gating.)
    const float ALPHA = 0.98f;
    att_roll_deg  = ALPHA * (att_roll_deg  + rate_roll  * RAD2DEG * dt) + (1.0f - ALPHA) * accel_roll;
    att_pitch_deg = ALPHA * (att_pitch_deg + rate_pitch * RAD2DEG * dt) + (1.0f - ALPHA) * accel_pitch;
}

// =============================================================================
// LOG de trajeto (LittleFS) — grava posições do GPS na flash interna
// =============================================================================
void startLog() {
    if (!LittleFS.begin(true)) {   // true = formata na 1a vez
        snprintf(g_log_report, sizeof(g_log_report), "LOG: LittleFS falhou");
        return;
    }
    // sequência: arquivo novo a cada boot (/trk1.csv, /trk2.csv, ...)
    uint32_t seq = 0;
    File sf = LittleFS.open("/seq.txt", "r");
    if (sf) { seq = sf.parseInt(); sf.close(); }
    seq++;
    sf = LittleFS.open("/seq.txt", "w");
    if (sf) { sf.print(seq); sf.close(); }

    snprintf(g_log_path, sizeof(g_log_path), "/trk%lu.csv", (unsigned long)seq);
    logFile = LittleFS.open(g_log_path, "w");
    if (logFile) {
        logFile.println("ms,lat,lon,alt_m,sats,spd_mps,crs_deg");
        logFile.flush();
        log_ok = true;
        snprintf(g_log_report, sizeof(g_log_report), "LOG: %s", g_log_path);
    } else {
        snprintf(g_log_report, sizeof(g_log_report), "LOG: open falhou");
    }
}

// =============================================================================
// WiFi AP + servidor web — baixar os logs no chão (desarmado)
// =============================================================================
void handleRoot() {
    String html = F("<html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
                    "<title>OpenRC FC logs</title></head><body style='font-family:sans-serif'>"
                    "<h2>OpenRC AeroLink - Logs de voo</h2><ul>");
    File root = LittleFS.open("/");
    File f = root.openNextFile();
    while (f) {
        String n = f.name();
        if (n.endsWith(".csv")) {
            html += "<li><a href='/dl?f=" + n + "'>" + n + "</a> (" + String(f.size()) + " B)</li>";
        }
        f = root.openNextFile();
    }
    html += F("</ul><p><a href='/clear' onclick=\"return confirm('Apagar todos os logs?')\">Apagar todos</a></p>"
              "</body></html>");
    server.send(200, "text/html", html);
}

void handleDownload() {
    if (!server.hasArg("f")) { server.send(400, "text/plain", "missing f"); return; }
    String name = server.arg("f");
    if (!name.startsWith("/")) name = "/" + name;
    File f = LittleFS.open(name, "r");
    if (!f) { server.send(404, "text/plain", "not found"); return; }
    server.streamFile(f, "text/csv");
    f.close();
}

void handleClear() {
    // para de gravar e apaga todos os .csv (power-cycle inicia log novo)
    log_ok = false;
    if (logFile) logFile.close();
    String names[32]; int n = 0;
    File root = LittleFS.open("/");
    File f = root.openNextFile();
    while (f && n < 32) {
        String nm = f.name();
        if (nm.endsWith(".csv")) names[n++] = nm.startsWith("/") ? nm : ("/" + nm);
        f = root.openNextFile();
    }
    for (int i = 0; i < n; i++) LittleFS.remove(names[i]);
    server.send(200, "text/html", "<html><body>Logs apagados. Reinicie o ESP p/ novo log. <a href='/'>voltar</a></body></html>");
}

void startWifiAP() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP);
    delay(100);                                   // estabiliza antes do softAP
    bool ok = WiFi.softAP(AP_SSID, AP_PASS);
    IPAddress ip = WiFi.softAPIP();
    snprintf(g_wifi_report, sizeof(g_wifi_report), "WiFi %s ip=%s",
             ok ? "ON" : "FALHOU", ip.toString().c_str());
    if (ok) {
        server.on("/", handleRoot);
        server.on("/dl", handleDownload);
        server.on("/clear", handleClear);
        server.begin();
        mavServer.begin();          // MAVLink TCP p/ Mission Planner (porta 5760)
        mavServer.setNoDelay(true);
    }
}

void stopWifiAP() {
    server.stop();
    mavClient.stop();
    mavServer.end();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    g_mav = (Stream*)&Serial;
    snprintf(g_wifi_report, sizeof(g_wifi_report), "WiFi off");
}

// =============================================================================
// PARÂMETROS — protocolo PARAM do MAVLink + persistência na NVS
// =============================================================================
void loadParams() {
    prefs.begin("fcparams", true);
    for (uint16_t i = 0; i < NPARAM; i++)
        *g_params[i].ptr = prefs.getFloat(g_params[i].name, *g_params[i].ptr);
    prefs.end();
}

void sendParam(uint16_t i) {
    mav_send_param_value(*g_mav, g_params[i].name, *g_params[i].ptr, NPARAM, i);
}

int findParam(const char* id) {          // id = 16 bytes (pode não terminar em \0)
    char name[17]; memcpy(name, id, 16); name[16] = 0;
    for (uint16_t i = 0; i < NPARAM; i++)
        if (strncmp(g_params[i].name, name, 16) == 0) return (int)i;
    return -1;
}

// PARAM_SET wire: float val[0..3], sys[4], comp[5], id[6..21], type[22]
void handleParamSet(const uint8_t* p) {
    float val; memcpy(&val, &p[0], 4);
    int i = findParam((const char*)&p[6]);
    if (i < 0) return;
    if (val < g_params[i].vmin) val = g_params[i].vmin;   // clamp de segurança
    if (val > g_params[i].vmax) val = g_params[i].vmax;
    *g_params[i].ptr = val;                                // aplica AO VIVO
    prefs.begin("fcparams", false);
    prefs.putFloat(g_params[i].name, val);                 // persiste na NVS
    prefs.end();
    sendParam((uint16_t)i);                                // ack p/ o GCS
}

// PARAM_REQUEST_READ wire: int16 idx[0..1], sys[2], comp[3], id[4..19]
void handleParamReqRead(const uint8_t* p) {
    int16_t idx; memcpy(&idx, &p[0], 2);
    int i = (idx >= 0 && idx < (int)NPARAM) ? idx : findParam((const char*)&p[4]);
    if (i >= 0) sendParam((uint16_t)i);
}

// Calibração one-shot do gyro (botão do MP) — re-zera o bias e nivela a atitude.
// Bloqueia ~0.4 s; só use no chão (cai em failsafe brevemente, sem problema).
void runGyroCalBlocking() {
    if (!mpu_ok) return;
    double sx = 0, sy = 0, sz = 0; const int N = 200;
    sensors_event_t a, g, t;
    for (int i = 0; i < N; i++) { if (mpu.getEvent(&a, &g, &t)) { sx += g.gyro.x; sy += g.gyro.y; sz += g.gyro.z; } delay(2); }
    gyro_bias_x = (float)(sx / N); gyro_bias_y = (float)(sy / N); gyro_bias_z = (float)(sz / N);
    att_roll_deg = 0; att_pitch_deg = 0;
    snprintf(g_calib_report, sizeof(g_calib_report), "MPU calibrado (MP)");
}

void handleMavRx() {
    switch (mavrx.msgid) {
        case MAVLINK_MSG_ID_PARAM_REQUEST_LIST:
            if (mavrx.crcOk(MAVLINK_CRC_PARAM_REQUEST_LIST))
                for (uint16_t i = 0; i < NPARAM; i++) sendParam(i);   // manda a lista toda
            break;
        case MAVLINK_MSG_ID_PARAM_REQUEST_READ:
            if (mavrx.crcOk(MAVLINK_CRC_PARAM_REQUEST_READ)) handleParamReqRead(mavrx.payload);
            break;
        case MAVLINK_MSG_ID_PARAM_SET:
            if (mavrx.crcOk(MAVLINK_CRC_PARAM_SET)) handleParamSet(mavrx.payload);
            break;
        case MAVLINK_MSG_ID_COMMAND_LONG:
            if (mavrx.crcOk(MAVLINK_CRC_COMMAND_LONG)) {
                uint16_t cmd; memcpy(&cmd, &mavrx.payload[28], 2);   // command @ offset 28 (uint16)
                if (cmd == MAV_CMD_PREFLIGHT_CALIBRATION) {
                    runGyroCalBlocking();
                    mav_send_command_ack(*g_mav, cmd, MAV_RESULT_ACCEPTED);
                } else {
                    // ACK genérico p/ o MP parar de re-tentar (REQUEST_MESSAGE, etc.)
                    mav_send_command_ack(*g_mav, cmd, MAV_RESULT_UNSUPPORTED);
                }
            }
            break;
    }
}

// =============================================================================
// SETUP
// =============================================================================
void setup() {
    // WiFi NÃO é inicializado no boot (fica off por padrão, baixo consumo).
    // Inicializar e desligar aqui (WIFI_OFF) quebrava o softAP depois — por isso
    // só ativamos o WiFi sob demanda em startWifiAP(). BT sempre off.
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

    loadParams();   // carrega params salvos na NVS (sobrescreve os defaults)

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
    // Buffer de RX grande (4 KB ~= 97 ms a 420k): segura os frames CRSF enquanto
    // o loop é bloqueado por escrita em flash (log), I2C (MPU), etc. — sem isso,
    // o buffer padrão de 256 B (~6 ms) estoura e o link "soluça"/cai em failsafe.
    Serial2.setRxBufferSize(4096);
    Serial2.begin(CRSF_BAUDRATE, SERIAL_8N1, PIN_CRSF_RX, PIN_CRSF_TX);
#if !MAVLINK_ENABLED
    Serial.printf("CRSF iniciado em UART2: RX=GPIO%d TX=GPIO%d @%lu baud\n",
                  PIN_CRSF_RX, PIN_CRSF_TX, (unsigned long)CRSF_BAUDRATE);
#endif

    // GPS na UART1 (GY-NEO6MV2, 9600 8N1) — buffer maior tb p/ não perder NMEA
    Serial1.setRxBufferSize(512);
    Serial1.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);

    // MPU6050 (não bloqueante: se faltar, segue como RX puro, só sem horizonte)
    Wire.begin(PIN_MPU_SDA, PIN_MPU_SCL);
    Wire.setClock(100000);   // 100 kHz é mais tolerante a pull-ups fracos
    Wire.setTimeOut(25);     // não trava o loop se o I2C do MPU der glitch (vibração)

    // --- Scan I2C: descobre o que realmente está no barramento (21/22) ---
    uint8_t addrs[8]; int found = 0;
    for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) { if (found < 8) addrs[found] = a; found++; }
    }
    if (found == 0) {
        snprintf(g_i2c_report, sizeof(g_i2c_report), "I2C(SDA%d/SCL%d): nenhum", PIN_MPU_SDA, PIN_MPU_SCL);
    } else {
        snprintf(g_i2c_report, sizeof(g_i2c_report), "I2C(SDA%d):", PIN_MPU_SDA);
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

    // Log de trajeto na flash interna (LittleFS)
    startLog();

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

        // Sticks normalizados para mixagem elevon (com swap roll<->pitch se ativo)
        float rollIn  = crsf_to_norm(crsf.getChannel(CRSF_IDX_AIL));
        float pitchIn = crsf_to_norm(crsf.getChannel(CRSF_IDX_ELE));
        if (swap_rp > 0.5f) { float tmp = rollIn; rollIn = pitchIn; pitchIn = tmp; }
        in_.ail_norm = rollIn;
        in_.ele_norm = pitchIn;

        // Switches: CH5 = arm (HIGH=armado), CH8 = modo (LOW=manual, HIGH=estab)
        in_.armed       = crsf_channel_to_us(crsf.getChannel(CRSF_IDX_ARM))  > SWITCH_THRESHOLD_US;
        in_.manual_mode = crsf_channel_to_us(crsf.getChannel(CRSF_IDX_MODE)) < SWITCH_THRESHOLD_US;
        // CH9 (botão momentâneo): calibra o MPU enquanto segurado
        calib_btn       = crsf_channel_to_us(crsf.getChannel(CRSF_IDX_CALIB)) > SWITCH_THRESHOLD_US;

        // LED aceso constante enquanto há link CRSF (apaga só em failsafe).
        // (antes togglava por frame -> flicker confuso que parecia "reset")
        digitalWrite(PIN_LED, HIGH);
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

    // LOG: grava posição do GPS a 1 Hz quando há fix (independe da telemetria)
    static uint32_t last_log_ms = 0;
    if (log_ok && now - last_log_ms >= 1000) {
        last_log_ms = now;
        if (gps.location.isValid()) {
            logFile.printf("%lu,%.7f,%.7f,%.1f,%u,%.1f,%.1f\n",
                (unsigned long)now, gps.location.lat(), gps.location.lng(),
                gps.altitude.isValid()    ? gps.altitude.meters()        : 0.0,
                gps.satellites.isValid()  ? (unsigned)gps.satellites.value() : 0,
                gps.speed.isValid()       ? gps.speed.mps()              : 0.0,
                gps.course.isValid()      ? gps.course.deg()             : 0.0);
            logFile.flush();   // garante que o ponto sobrevive a queda de energia
        }
    }

    // WiFi p/ baixar log: só no CHÃO (desarmado) e com o switch CH10 acima de 50%.
    bool wifi_want = !in_.armed &&
        (crsf_channel_to_us(crsf.getChannel(CRSF_IDX_WIFI)) > WIFI_ON_US);
    if (wifi_want && !wifi_on)      { startWifiAP(); wifi_on = true; }
    else if (!wifi_want && wifi_on) { stopWifiAP();  wifi_on = false; }
    if (wifi_on) {
        server.handleClient();
        // aceita 1 cliente MAVLink TCP (Mission Planner em 192.168.4.1:5760)
        if (mavServer.hasClient()) {
            if (mavClient && mavClient.connected()) mavServer.available().stop();
            else                                     mavClient = mavServer.available();
        }
        g_mav = (mavClient && mavClient.connected()) ? (Stream*)&mavClient : (Stream*)&Serial;
    } else {
        g_mav = (Stream*)&Serial;
    }

    // MAVLink RX (params do Mission Planner) — alimenta o parser com WiFi e USB.
    // g_mav já está setado, então as respostas (PARAM_VALUE) vão pra origem certa.
    while (mavClient && mavClient.available()) { if (mavrx.feed((uint8_t)mavClient.read())) handleMavRx(); }
    while (Serial.available())                 { if (mavrx.feed((uint8_t)Serial.read()))    handleMavRx(); }

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
        mav_send_heartbeat(*g_mav,base, state);
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
        mav_send_rc_channels_raw(*g_mav,now, ch);
    }

    // ---- ATTITUDE a 25 Hz (horizonte artificial no MP) ----
    static uint32_t last_att_ms = 0;
    if (now - last_att_ms >= 40) {
        last_att_ms = now;
        mav_send_attitude(*g_mav,now,
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
        mav_send_gps_raw_int(*g_mav,(uint64_t)now * 1000ULL, fix, lat, lon, alt, vel, cog, sats);
        if (fixv) {  // só põe ícone no mapa quando tem fix (evita 0,0)
            uint16_t hdg = gps.course.isValid() ? (uint16_t)(gps.course.deg() * 100.0) : 65535;
            mav_send_global_position_int(*g_mav,now, lat, lon, alt, alt, 0, 0, 0, hdg);
        }
    }

    // ---- STATUSTEXT a cada 3 s, rotativo: I2C/MPU, PWM, GPS (aba Messages) ----
    static uint32_t last_status_ms = 0;
    static uint8_t  status_idx = 0;
    if (now - last_status_ms >= 3000) {
        last_status_ms = now;
        char buf[50];
        const char *msg; uint8_t sev = MAV_SEVERITY_INFO;
        switch (status_idx++ % 6) {
            case 0: msg = g_i2c_report; sev = mpu_ok ? MAV_SEVERITY_INFO : MAV_SEVERITY_CRITICAL; break;
            case 1: msg = g_pwm_report; sev = strstr(g_pwm_report, "FALH") ? MAV_SEVERITY_CRITICAL : MAV_SEVERITY_INFO; break;
            case 2:
                // rx = bytes recebidos do GPS: 0 = nada chegando (fiação/energia/baud);
                // subindo = GPS vivo e falando (só falta fix/satélite).
                snprintf(buf, sizeof(buf), "GPS: %s sats=%u rx=%lu",
                         gps.location.isValid() ? "FIX" : "nofix",
                         gps.satellites.isValid() ? (unsigned)gps.satellites.value() : 0,
                         (unsigned long)gps.charsProcessed());
                msg = buf; break;
            case 3:
                // Modo de voo ativo: MANUAL (CH8 baixo) ou ESTAB/auto-nível (CH8 alto)
                snprintf(buf, sizeof(buf), "MODO: %s | %s | roll=%d pitch=%d",
                         (!in_.manual_mode && mpu_ok) ? "AUTO-NIVEL" : "MANUAL",
                         in_.armed ? "ARMADO" : "DESARMADO",
                         (int)att_roll_deg, (int)att_pitch_deg);
                msg = buf; break;
            case 4:
                snprintf(buf, sizeof(buf), "%s | %s", g_log_report, g_wifi_report);
                msg = buf; break;
            default:
                msg = g_calib_report; break;
        }
        mav_send_statustext(*g_mav,sev, msg);
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
