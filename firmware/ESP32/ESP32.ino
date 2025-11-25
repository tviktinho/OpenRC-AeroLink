#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <ESP32Servo.h>
#include <Preferences.h>
// MAVLink headers - gere/mantenha mavlink/common na pasta de includes do projeto
#include "mavlink/common/mavlink.h"
// PINAGENS
const uint8_t PIN_NRF_CE = 4;
const uint8_t PIN_NRF_CSN = 5;
const uint8_t PIN_MPU_SDA = 21;
const uint8_t PIN_MPU_SCL = 22;
const uint8_t PIN_UART_RX = 26; // RX2 (entrada do ESP32 vindo do RPi/GCS)
const uint8_t PIN_UART_TX = 27; // TX2 (saida MAVLink)
// Saídas de controle
const uint8_t PIN_ESC = 12;
const uint8_t PIN_SERVO_L = 13;
const uint8_t PIN_SERVO_R = 14;

// Objetos
RF24 radio(PIN_NRF_CE, PIN_NRF_CSN);
Adafruit_MPU6050 mpu;
Preferences preferences;
Servo esc, srvL, srvR;

// Rádio
struct Packet { uint8_t p[8]; uint8_t s1; uint8_t s2; } pkt;
const byte RADIO_ADDR[6] = "00001";

// AHRS
float att_roll = 0.0f, att_pitch = 0.0f; // graus
const float AHRS_ALPHA = 0.98f;
const float RAD2DEG = 57.295779513082320876f;
uint64_t last_att_us = 0;

// Timers
uint64_t last_heartbeat_ms = 0;
uint64_t last_mav_att_ms = 0;
const uint32_t HEARTBEAT_MS = 1000;
const uint32_t ATT_SEND_MS = 50; // 20 Hz

// RC state
int rc_us[8];
uint32_t last_radio_ms = 0;

// Params (saved)
int g_rc_trim[8];
int g_servo_trim_l = 0;
int g_servo_trim_r = 0;
float g_deadzone = 0.05f;

// MAVLink ids
const uint8_t MAV_SYS_ID = 1;
const uint8_t MAV_COMP_ID = 200;

// MAVLink parser state
mavlink_message_t mav_msg;
mavlink_status_t mav_status;

// Helpers
inline int byteToUs(uint8_t v) { return 1000 + ((int)v * 1000) / 255; }

void saveRcToNVS() {
  preferences.begin("fc-rc", false);
  for (int i=0;i<8;i++) preferences.putInt((String("rtrim")+i).c_str(), g_rc_trim[i]);
  preferences.putInt("srv_l", g_servo_trim_l);
  preferences.putInt("srv_r", g_servo_trim_r);
  preferences.putFloat("deadz", g_deadzone);
  preferences.end();
}

void loadRcFromNVS() {
  preferences.begin("fc-rc", true);
  for (int i=0;i<8;i++) g_rc_trim[i] = preferences.getInt((String("rtrim")+i).c_str(), 0);
  g_servo_trim_l = preferences.getInt("srv_l", 0);
  g_servo_trim_r = preferences.getInt("srv_r", 0);
  g_deadzone = preferences.getFloat("deadz", 0.05f);
  preferences.end();
}

// Envia um MAVLink pela Serial2
void send_mavlink_msg(const mavlink_message_t &msg) {
  uint8_t buf[MAVLINK_MAX_PACKET_LEN];
  uint16_t len = mavlink_msg_to_send_buffer(buf, &msg);
  Serial2.write(buf, len);
}

// Envia HEARTBEAT
void send_heartbeat() {
  mavlink_message_t msg;
  mavlink_msg_heartbeat_pack(MAV_SYS_ID, MAV_COMP_ID, &msg,
    MAV_TYPE_GENERIC, MAV_AUTOPILOT_GENERIC, 0, 0, MAV_STATE_ACTIVE);
  send_mavlink_msg(msg);
}

// Envia ATTITUDE (roll/pitch in RAD)
void send_attitude() {
  mavlink_message_t msg;
  uint32_t t = (uint32_t)(esp_timer_get_time() / 1000ULL);
  float roll_r = att_roll * (3.14159265f/180.0f);
  float pitch_r = att_pitch * (3.14159265f/180.0f);
  float yaw_r = 0.0f;
  // No campo rollspeed/pitchspeed/yawspeed deixamos 0 por enquanto
  mavlink_msg_attitude_pack(MAV_SYS_ID, MAV_COMP_ID, &msg, t, roll_r, pitch_r, yaw_r, 0.0f, 0.0f, 0.0f);
  send_mavlink_msg(msg);
}

// Envia RC_CHANNELS_RAW com canais convertidos para us
void send_rc_channels_raw() {
  mavlink_message_t msg;
  uint32_t t = (uint32_t)(esp_timer_get_time() / 1000ULL);
  // port=0, chan1..8
  mavlink_msg_rc_channels_raw_pack(MAV_SYS_ID, MAV_COMP_ID, &msg, t,
    0,
    rc_us[0], rc_us[1], rc_us[2], rc_us[3], rc_us[4], rc_us[5], rc_us[6], rc_us[7],
    0);
  send_mavlink_msg(msg);
}

// Tratamento de PARAM_SET
void handle_param_set(const mavlink_message_t &msg) {
  mavlink_param_set_t ps;
  mavlink_msg_param_set_decode(&msg, &ps);
  char pname[17]; memcpy(pname, ps.param_id, 16); pname[16]=0;
  float val = ps.param_value;
  // Normalize name uppercase
  for (int i=0;i<16;i++) if (pname[i]>= 'a' && pname[i]<='z') pname[i] -= 32;
  // Mapeamento simples: TRIM1..TRIM8, SRV_L, SRV_R, DEADZ
  if (strncmp(pname, "TRIM", 4)==0) {
    int idx = atoi(pname+4)-1; if (idx>=0 && idx<8) { g_rc_trim[idx] = (int)val; saveRcToNVS(); }
  } else if (strcmp(pname, "SRV_L")==0) { g_servo_trim_l = (int)val; saveRcToNVS(); }
    else if (strcmp(pname, "SRV_R")==0) { g_servo_trim_r = (int)val; saveRcToNVS(); }
    else if (strcmp(pname, "DEADZ")==0) { g_deadzone = val; if (g_deadzone<0) g_deadzone=0; if (g_deadzone>0.45f) g_deadzone=0.45f; saveRcToNVS(); }
  // Responder com PARAM_VALUE para confirmar
  mavlink_message_t out;
  // usa MAVLINK_TYPE_FLOAT conforme gerado pela biblioteca C do MAVLink
  mavlink_msg_param_value_pack(MAV_SYS_ID, MAV_COMP_ID, &out, pname, val, MAVLINK_TYPE_FLOAT, 1, 0);
  send_mavlink_msg(out);
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("ESP32 MAVLink Autopilot - Inicializando");

  // Serial2 para MAVLink
  Serial2.begin(115200, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);

  // Servos
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  esc.attach(PIN_ESC, 1000, 2000);
  srvL.attach(PIN_SERVO_L, 1000, 2000);
  srvR.attach(PIN_SERVO_R, 1000, 2000);

  // MPU6050
  Wire.begin(PIN_MPU_SDA, PIN_MPU_SCL);
  if (!mpu.begin()) {
    Serial.println("MPU6050 não detectado!");
    while (1);
  }
  // usa constantes providas pela biblioteca Adafruit MPU6050
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);

  // NRF24
  if (!radio.begin()) {
    Serial.println("NRF24 não detectado!");
    while (1);
  }
  radio.openReadingPipe(0, RADIO_ADDR);
  radio.setPALevel(RF24_PA_LOW);
  radio.startListening();

  // Load params
  loadRcFromNVS();
}

void loop() {
  uint64_t now_us = esp_timer_get_time();
  uint64_t now_ms = now_us / 1000ULL;

  // Leitura do rádio
  if (radio.available()) {
    radio.read(&pkt, sizeof(pkt));
    for (int i = 0; i < 8; i++) {
      rc_us[i] = byteToUs(pkt.p[i]) + g_rc_trim[i];
    }
    last_radio_ms = now_ms;
  }

  // AHRS (filtro complementar)
  if (now_us - last_att_us >= 20000) { // 50 Hz
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    float dt = (now_us - last_att_us) / 1000000.0f;
    last_att_us = now_us;

    float acc_roll = atan2(a.acceleration.y, a.acceleration.z) * RAD2DEG;
    float acc_pitch = atan2(-a.acceleration.x, sqrt(a.acceleration.y * a.acceleration.y + a.acceleration.z * a.acceleration.z)) * RAD2DEG;

    att_roll = AHRS_ALPHA * (att_roll + g.gyro.x * dt) + (1 - AHRS_ALPHA) * acc_roll;
    att_pitch = AHRS_ALPHA * (att_pitch + g.gyro.y * dt) + (1 - AHRS_ALPHA) * acc_pitch;
  }

  // Envia HEARTBEAT
  if (now_ms - last_heartbeat_ms >= HEARTBEAT_MS) {
    last_heartbeat_ms = now_ms;
    send_heartbeat();
  }

  // Envia ATTITUDE
  if (now_ms - last_mav_att_ms >= ATT_SEND_MS) {
    last_mav_att_ms = now_ms;
    send_attitude();
  }

  // Envia RC_CHANNELS_RAW
  send_rc_channels_raw();

  // Processa mensagens MAVLink recebidas
  while (Serial2.available() > 0) {
    uint8_t c = Serial2.read();
    if (mavlink_parse_char(MAVLINK_COMM_0, c, &mav_msg, &mav_status)) {
      if (mav_msg.msgid == MAVLINK_MSG_ID_PARAM_SET) {
        handle_param_set(mav_msg);
      }
    }
  }

  // Controle dos servos
  int esc_out = rc_us[2];
  int srvL_out = rc_us[0] + g_servo_trim_l;
  int srvR_out = rc_us[1] + g_servo_trim_r;

  esc.writeMicroseconds(esc_out);
  srvL.writeMicroseconds(srvL_out);
  srvR.writeMicroseconds(srvR_out);
}