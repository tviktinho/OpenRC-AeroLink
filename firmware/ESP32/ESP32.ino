/**
 * FIRMWARE ESP32 — RX + ESTABILIZADOR
 * Compatível com NANO_TX (OpenRC AeroLink)
 *
 * MODOS DE VOO (via switch s1 do transmissor):
 *   s1 = 0 → MANUAL  (elevon mixing puro, sem correção)
 *   s1 = 1 → ESTABILIZADO (rate-mode: gyro corrige desvios)
 *
 * MAVLink: comentado, pronto para integração futura com Ardupilot.
 */

#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <ESP32Servo.h>
#include <Preferences.h>

// [MAVLINK DESATIVADO] — Descomente para reativar
// #include "mavlink/common/mavlink.h"

// =========================================================================
// === PINAGEM ===
// =========================================================================
const uint8_t PIN_NRF_CE  = 4;
const uint8_t PIN_NRF_CSN = 5;
const uint8_t PIN_MPU_SDA = 21;
const uint8_t PIN_MPU_SCL = 22;
const uint8_t PIN_ESC     = 12;
const uint8_t PIN_SERVO_L = 13;
const uint8_t PIN_SERVO_R = 14;
const uint8_t PIN_AUX1    = 27;   // canal auxiliar 1 (p[4])
const uint8_t PIN_AUX2    = 26;   // canal auxiliar 2 (p[5])
const uint8_t PIN_AUX3    = 25;   // canal auxiliar 3 (p[6])

// =========================================================================
// === OBJETOS ===
// =========================================================================
RF24 radio(PIN_NRF_CE, PIN_NRF_CSN);
Adafruit_MPU6050 mpu;
Preferences preferences;
Servo esc, srvL, srvR, aux1, aux2, aux3;

// =========================================================================
// === RÁDIO ===
// =========================================================================
struct __attribute__((packed)) Packet {
  uint8_t p[8];   // 8 canais analógicos (0..255)
  uint8_t s1;     // Switch 1 (0=manual, 1=estabilizado)
  uint8_t s2;     // Switch 2 (reservado)
} pkt;
const byte RADIO_ADDR[6] = "00001";

// Mapeamento de canais (igual 14-BRIZA e TX_NANO)
const uint8_t CH_THR  = 0;    // throttle
const uint8_t CH_ELE  = 3;    // pitch (elevator)
const uint8_t CH_AIL  = 2;    // roll  (aileron)
const uint8_t CH_AUX1 = 4;    // auxiliar 1 → GPIO 27
const uint8_t CH_AUX2 = 5;    // auxiliar 2 → GPIO 26
const uint8_t CH_AUX3 = 6;    // auxiliar 3 → GPIO 25

// =========================================================================
// === CONFIGURAÇÕES DE VOO ===
// =========================================================================
const int PWM_MIN  = 1000;
const int PWM_MAX  = 2000;
const int PWM_IDLE = 1000;

// Ganhos elevon (throws) — modo manual
float G_E = 2.55f;   // ganho de pitch
float G_A = 2.55f;   // ganho de roll

// Expo
float EXPO_E = 0.25f;
float EXPO_A = 0.25f;

// Diferencial (mais UP que DOWN)
float DIFF = -0.30f;

// Reflex (trim permanente para cima)
int REFLEX_US = +30;

// === ESTABILIZAÇÃO (rate-mode) ===
// Ganho proporcional do gyro (graus/s → correção em µs)
// Quanto maior, mais agressiva a estabilização
float STAB_GAIN_ROLL  = 1.5f;   // P-gain roll
float STAB_GAIN_PITCH = 1.5f;   // P-gain pitch
float MAX_RATE_DPS    = 300.0f;  // taxa máxima em °/s que o stick comanda

// Failsafe
const uint32_t RX_TIMEOUT_MS = 200;

// =========================================================================
// === ESTADO GLOBAL ===
// =========================================================================
// MPU
bool mpu_ok = false;               // se MPU foi detectado
float att_roll = 0.0f, att_pitch = 0.0f;
const float RAD2DEG = 57.295779513082320876f;
float gyro_roll_offset  = 0.0f;    // offset de calibração
float gyro_pitch_offset = 0.0f;

// Rádio
uint32_t last_radio_ms = 0;
bool manual_mode = true;           // começa em manual por segurança

// RC state (para telemetria/debug)
int rc_us[8];

// Params (NVS)
int g_rc_trim[8];
int g_servo_trim_l = 0;
int g_servo_trim_r = 0;
float g_deadzone = 0.05f;

// [MAVLINK] — desativado
// const uint8_t MAV_SYS_ID = 1;
// const uint8_t MAV_COMP_ID = 200;
// mavlink_message_t mav_msg;
// mavlink_status_t mav_status;

// =========================================================================
// === HELPERS ===
// =========================================================================
inline float mapByteToNorm(uint8_t b) { return ((int)b - 128) / 127.0f; }

inline int normToUs(float x) {
  if (x < -1) x = -1; if (x > +1) x = +1;
  return (int)(1500 + x * 400);
}

inline float applyExpo(float v, float e) {
  return v * (1.0f - e) + v * v * v * e;
}

inline int byteToUs(uint8_t v) { return 1000 + ((int)v * 1000) / 255; }

// =========================================================================
// === NVS (salvar/carregar trims) ===
// =========================================================================
void saveRcToNVS() {
  preferences.begin("fc-rc", false);
  for (int i = 0; i < 8; i++) preferences.putInt((String("rtrim") + i).c_str(), g_rc_trim[i]);
  preferences.putInt("srv_l", g_servo_trim_l);
  preferences.putInt("srv_r", g_servo_trim_r);
  preferences.putFloat("deadz", g_deadzone);
  preferences.end();
}

void loadRcFromNVS() {
  preferences.begin("fc-rc", true);
  for (int i = 0; i < 8; i++) g_rc_trim[i] = preferences.getInt((String("rtrim") + i).c_str(), 0);
  g_servo_trim_l = preferences.getInt("srv_l", 0);
  g_servo_trim_r = preferences.getInt("srv_r", 0);
  g_deadzone = preferences.getFloat("deadz", 0.05f);
  preferences.end();
}

// =========================================================================
// === CALIBRAÇÃO DO GYRO ===
// =========================================================================
void calibrateGyro() {
  if (!mpu_ok) return;
  Serial.println("Calibrando gyro... mantenha o avião parado!");
  float sum_roll = 0, sum_pitch = 0;
  sensors_event_t a, g, temp;
  const int SAMPLES = 500;
  for (int i = 0; i < SAMPLES; i++) {
    mpu.getEvent(&a, &g, &temp);
    // MPU montado com X=horizontal, Y=vertical → Z=frente
    // Roll = rotação em Z (eixo frontal), Pitch = rotação em X (eixo lateral)
    sum_roll  += g.gyro.z * RAD2DEG;
    sum_pitch += g.gyro.x * RAD2DEG;
    delay(2);
  }
  gyro_roll_offset  = sum_roll  / SAMPLES;
  gyro_pitch_offset = sum_pitch / SAMPLES;
  Serial.printf("Gyro offsets: roll=%.2f pitch=%.2f\n", gyro_roll_offset, gyro_pitch_offset);
}

/* =========================================================================
 * [MAVLINK DESATIVADO] — Todas as funções MAVLink comentadas.
 * Descomente este bloco para reativar integração com Ardupilot/GCS.
 * =========================================================================

void send_mavlink_msg(const mavlink_message_t &msg) {
  uint8_t buf[MAVLINK_MAX_PACKET_LEN];
  uint16_t len = mavlink_msg_to_send_buffer(buf, &msg);
  Serial2.write(buf, len);
}

void send_heartbeat() {
  mavlink_message_t msg;
  mavlink_msg_heartbeat_pack(MAV_SYS_ID, MAV_COMP_ID, &msg,
    MAV_TYPE_GENERIC, MAV_AUTOPILOT_GENERIC, 0, 0, MAV_STATE_ACTIVE);
  send_mavlink_msg(msg);
}

void send_attitude() {
  mavlink_message_t msg;
  uint32_t t = (uint32_t)(esp_timer_get_time() / 1000ULL);
  float roll_r = att_roll * (3.14159265f/180.0f);
  float pitch_r = att_pitch * (3.14159265f/180.0f);
  mavlink_msg_attitude_pack(MAV_SYS_ID, MAV_COMP_ID, &msg, t,
    roll_r, pitch_r, 0.0f, 0.0f, 0.0f, 0.0f);
  send_mavlink_msg(msg);
}

void send_rc_channels_raw() {
  mavlink_message_t msg;
  uint32_t t = (uint32_t)(esp_timer_get_time() / 1000ULL);
  mavlink_msg_rc_channels_raw_pack(MAV_SYS_ID, MAV_COMP_ID, &msg, t, 0,
    rc_us[0], rc_us[1], rc_us[2], rc_us[3],
    rc_us[4], rc_us[5], rc_us[6], rc_us[7], 0);
  send_mavlink_msg(msg);
}

void handle_param_set(const mavlink_message_t &msg) {
  mavlink_param_set_t ps;
  mavlink_msg_param_set_decode(&msg, &ps);
  char pname[17]; memcpy(pname, ps.param_id, 16); pname[16]=0;
  float val = ps.param_value;
  for (int i=0;i<16;i++) if (pname[i]>='a' && pname[i]<='z') pname[i]-=32;
  if (strncmp(pname,"TRIM",4)==0) {
    int idx=atoi(pname+4)-1; if(idx>=0&&idx<8){g_rc_trim[idx]=(int)val;saveRcToNVS();}
  } else if(strcmp(pname,"SRV_L")==0){g_servo_trim_l=(int)val;saveRcToNVS();}
    else if(strcmp(pname,"SRV_R")==0){g_servo_trim_r=(int)val;saveRcToNVS();}
    else if(strcmp(pname,"DEADZ")==0){g_deadzone=val;if(g_deadzone<0)g_deadzone=0;if(g_deadzone>0.45f)g_deadzone=0.45f;saveRcToNVS();}
  mavlink_message_t out;
  mavlink_msg_param_value_pack(MAV_SYS_ID,MAV_COMP_ID,&out,pname,val,MAVLINK_TYPE_FLOAT,1,0);
  send_mavlink_msg(out);
}

*/ // FIM MAVLINK DESATIVADO

// =========================================================================
// === SETUP ===
// =========================================================================
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("ESP32 RX + Estabilizador - Inicializando");

  // [MAVLINK] Serial2 — desativado
  // Serial2.begin(115200, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);

  // Servos / ESC (6 canais PWM)
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  esc.setPeriodHertz(50);
  srvL.setPeriodHertz(50);
  srvR.setPeriodHertz(50);
  aux1.setPeriodHertz(50);
  aux2.setPeriodHertz(50);
  aux3.setPeriodHertz(50);
  esc.attach(PIN_ESC, PWM_MIN, PWM_MAX);
  srvL.attach(PIN_SERVO_L, PWM_MIN, PWM_MAX);
  srvR.attach(PIN_SERVO_R, PWM_MIN, PWM_MAX);
  aux1.attach(PIN_AUX1, PWM_MIN, PWM_MAX);
  aux2.attach(PIN_AUX2, PWM_MIN, PWM_MAX);
  aux3.attach(PIN_AUX3, PWM_MIN, PWM_MAX);

  // Arma ESC com throttle baixo
  for (int i = 0; i < 100; ++i) {
    esc.writeMicroseconds(PWM_IDLE);
    delay(10);
  }

  // Posição inicial dos servos (neutro + reflex)
  srvL.writeMicroseconds(1500 + REFLEX_US);
  srvR.writeMicroseconds(1500 + REFLEX_US);
  aux1.writeMicroseconds(1500);
  aux2.writeMicroseconds(1500);
  aux3.writeMicroseconds(1500);

  // MPU6050 — NÃO BLOQUEANTE (continua como RX puro se falhar)
  Wire.begin(PIN_MPU_SDA, PIN_MPU_SCL);
  Wire.setTimeOut(10); // timeout I2C de 10ms para evitar travamentos
  if (mpu.begin()) {
    mpu_ok = true;
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    Serial.println("MPU6050 OK — calibrando gyro...");
    calibrateGyro();
  } else {
    mpu_ok = false;
    Serial.println("MPU6050 não detectado! Modo manual apenas.");
  }

  // NRF24
  if (!radio.begin()) {
    Serial.println("NRF24 não detectado!");
    while (1);
  }
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_250KBPS);
  radio.setChannel(76);
  radio.setAutoAck(false);
  radio.openReadingPipe(0, RADIO_ADDR);
  radio.startListening();

  // Load params
  loadRcFromNVS();
  last_radio_ms = millis();

  Serial.println("Sistema pronto!");
  Serial.printf("Modo: %s | MPU: %s\n",
    manual_mode ? "MANUAL" : "ESTABILIZADO",
    mpu_ok ? "OK" : "DESCONECTADO");
}

// =========================================================================
// === LOOP PRINCIPAL ===
// =========================================================================
void loop() {
  uint32_t now_ms = millis();

  // ========== 1. LEITURA DO RÁDIO ==========
  bool got = false;
  while (radio.available()) {
    radio.read(&pkt, sizeof(pkt));
    got = true;
  }

  if (got) {
    last_radio_ms = now_ms;

    // Atualiza rc_us (para debug/telemetria)
    for (int i = 0; i < 8; i++) {
      rc_us[i] = byteToUs(pkt.p[i]) + g_rc_trim[i];
    }

    // Switch s1: modo de voo (0=manual, 1=estabilizado)
    manual_mode = (pkt.s1 == 0);
  }

  // ========== 2. FAILSAFE ==========
  if (now_ms - last_radio_ms > RX_TIMEOUT_MS) {
    esc.writeMicroseconds(PWM_IDLE);
    srvL.writeMicroseconds(1500 + REFLEX_US);
    srvR.writeMicroseconds(1500 + REFLEX_US);
    aux1.writeMicroseconds(1500);
    aux2.writeMicroseconds(1500);
    aux3.writeMicroseconds(1500);
    return; // sai do loop, não processa nada
  }

  // ========== 3. LEITURA DO MPU (se disponível) ==========
  float gyro_roll_dps  = 0.0f;
  float gyro_pitch_dps = 0.0f;

  if (mpu_ok) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    // MPU montado com X=horizontal, Y=vertical → Z=frente
    gyro_roll_dps  = (g.gyro.z * RAD2DEG) - gyro_roll_offset;   // roll = eixo Z (frente)
    gyro_pitch_dps = (g.gyro.x * RAD2DEG) - gyro_pitch_offset;  // pitch = eixo X (lateral)
  }

  // ========== 4. CONTROLE DOS SERVOS ==========
  if (!got) return; // sem pacote novo, mantém último estado

  // Throttle: 0..255 → 1000..2000
  int pwm_thr = map(pkt.p[CH_THR], 0, 255, PWM_MIN, PWM_MAX);
  pwm_thr = constrain(pwm_thr, PWM_MIN, PWM_MAX);

  // Sticks normalizados com expo
  float E = applyExpo(mapByteToNorm(pkt.p[CH_ELE]), EXPO_E);
  float A = applyExpo(mapByteToNorm(pkt.p[CH_AIL]), EXPO_A);

  int usL, usR;

  if (manual_mode || !mpu_ok) {
    // ===== MODO MANUAL (elevon mixing puro, como 14-BRIZA) =====
    float eCmd = E * G_E;
    float aCmd = A * G_A;

    float L = eCmd - aCmd;
    float R = eCmd + aCmd;

    // Diferencial
    if (L < 0) L *= (1.0f - DIFF);
    if (R < 0) R *= (1.0f - DIFF);

    usL = normToUs(L) + REFLEX_US + g_servo_trim_l;
    usR = normToUs(R) + REFLEX_US + g_servo_trim_r;

  } else {
    // ===== MODO ESTABILIZADO (rate-mode com gyro) =====
    // O stick comanda uma taxa desejada (°/s)
    // O gyro mede a taxa real
    // A correção é: erro = desejado - medido
    float desired_roll  = A * MAX_RATE_DPS;   // stick → taxa desejada
    float desired_pitch = E * MAX_RATE_DPS;

    float error_roll  = desired_roll  - gyro_roll_dps;
    float error_pitch = desired_pitch - gyro_pitch_dps;

    // Correção proporcional (P-controller)
    float corr_roll  = error_roll  * STAB_GAIN_ROLL;
    float corr_pitch = error_pitch * STAB_GAIN_PITCH;

    // Elevon mixing com correção
    float L = -corr_pitch + corr_roll;
    float R = -corr_pitch - corr_roll;

    // Diferencial
    if (L < 0) L *= (1.0f - DIFF);
    if (R < 0) R *= (1.0f - DIFF);

    usL = constrain(1500 + (int)L + REFLEX_US + g_servo_trim_l, PWM_MIN, PWM_MAX);
    usR = constrain(1500 + (int)R + REFLEX_US + g_servo_trim_r, PWM_MIN, PWM_MAX);
  }

  // Satura 1000..2000
  usL = constrain(usL, PWM_MIN, PWM_MAX);
  usR = constrain(usR, PWM_MIN, PWM_MAX);

  // Saídas — elevon + ESC
  esc.writeMicroseconds(pwm_thr);
  srvL.writeMicroseconds(usL);
  srvR.writeMicroseconds(usR);

  // Saídas — canais auxiliares (passthrough direto do pacote)
  aux1.writeMicroseconds(byteToUs(pkt.p[CH_AUX1]));
  aux2.writeMicroseconds(byteToUs(pkt.p[CH_AUX2]));
  aux3.writeMicroseconds(byteToUs(pkt.p[CH_AUX3]));
}