/**
 * FIRMWARE DE VOO AIO (FC + RX) - VERSÃO INTEGRADA
 * Recursos: Voo, Rádio, Trims Digitais, Calibração RC, Telemetria
 */

#include <SPI.h>
#include <RF24.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <PID_v1.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// === PINAGEM ===
const uint8_t PIN_ESC = 12;
const uint8_t PIN_SERVO_L = 13;
const uint8_t PIN_SERVO_R = 14;

const uint8_t PIN_NRF_CE = 4;
const uint8_t PIN_NRF_CSN = 5;
const uint8_t PIN_MPU_SDA = 21;
const uint8_t PIN_MPU_SCL = 22;

// UART RASPBERRY PI
const uint8_t PIN_UART_RX = 26;
const uint8_t PIN_UART_TX = 27;

// === OBJETOS ===
RF24 radio(PIN_NRF_CE, PIN_NRF_CSN);
Servo esc, srvL, srvR;
Adafruit_MPU6050 mpu;
Preferences preferences;
SemaphoreHandle_t g_pid_mutex;
SemaphoreHandle_t g_serial_mutex; // NOVO: Protege a porta Serial2

// === CONFIGS VOO ===
const int PWM_MIN = 1000, PWM_MAX = 2000, PWM_IDLE = 1000;
float G_E = 1.0, G_A = 1.0;
float EXPO_E = 0.5, EXPO_A = 0.5, DIFF = 0.50;
int REFLEX_US = -30;
const float MAX_RATE_DPS = 300;
const float MANUAL_GAIN = 300.0;

// === RÁDIO ===
const byte RADIO_ADDRESS[6] = "00001";

struct __attribute__((packed)) PacketRF
{
  uint8_t p[8];
  uint8_t s1;
  uint8_t s2;
} pkt;

const uint8_t CH_THR = 0;
const uint8_t CH_ELE = 3;
const uint8_t CH_AIL = 2;

const uint32_t RX_TIMEOUT_MS = 200;
const int LOOP_FREQ_HZ = 333;
const TickType_t xLoopFrequency = pdMS_TO_TICKS(1000 / LOOP_FREQ_HZ);

// === VARIÁVEIS GLOBAIS ===
volatile double Kp_roll = 1.5, Ki_roll = 0.1, Kd_roll = 0.05;
volatile double Kp_pitch = 1.5, Ki_pitch = 0.1, Kd_pitch = 0.05;

volatile float g_cmd_roll = 0.0f, g_cmd_pitch = 0.0f;
volatile int g_cmd_throttle_us = PWM_IDLE;
volatile bool g_failsafe_active = true;
volatile bool g_manual_mode = false;
volatile uint32_t g_last_radio_packet_ms = 0;

// Requests entre cores
volatile bool g_request_gyro_cal = false;
volatile bool g_request_rc_cal = false;
volatile bool g_request_center_servos = false;
volatile bool g_request_center_and_save = false;

volatile float g_telemetry_roll = 0.0;
volatile float g_telemetry_pitch = 0.0;

// Calibração RC e Trims
volatile int g_rc_min[8];
volatile int g_rc_max[8];
volatile int g_rc_center[8];
volatile int g_rc_trim[8];
volatile int g_servo_trim_l = 0;
volatile int g_servo_trim_r = 0;
volatile bool g_rc_calibrating = false;

// Deadzone para sticks (0..0.5 típico)
volatile float g_deadzone = 0.05f;

volatile double g_gyro_roll_offset = 0.0;
volatile double g_gyro_pitch_offset = 0.0;
double g_setpoint_roll, g_input_roll, g_output_roll;
double g_setpoint_pitch, g_input_pitch, g_output_pitch;

float att_roll = 0.0f;          // ângulo estimado (graus)
float att_pitch = 0.0f;         // ângulo estimado (graus)
uint64_t last_att_us = 0;       // timestamp microsegundos
const float AHRS_ALPHA = 0.98f; // peso do giroscópio no filtro complementar
const float RAD2DEG = 57.295779513082320876f;

PID pid_roll(&g_input_roll, &g_output_roll, &g_setpoint_roll, Kp_roll, Ki_roll, Kd_roll, DIRECT);
PID pid_pitch(&g_input_pitch, &g_output_pitch, &g_setpoint_pitch, Kp_pitch, Ki_pitch, Kd_pitch, DIRECT);

// =========================================================================
// === FUNÇÕES AUXILIARES ===
// =========================================================================

// Helper para enviar JSON seguro (Thread Safe)
void sendJsonSerial(const JsonDocument &doc)
{
  if (xSemaphoreTake(g_serial_mutex, portMAX_DELAY))
  {
    serializeJson(doc, Serial2);
    Serial2.println();
    xSemaphoreGive(g_serial_mutex);
  }
}

void armESC_safety()
{
  for (int i = 0; i < 100; ++i)
  {
    esc.writeMicroseconds(PWM_IDLE);
    delay(10);
  }
}
inline float mapByteToNorm(uint8_t b) { return ((int)b - 128) / 127.0f; }
inline float applyExpo(float v, float e) { return v * (1.0f - e) + v * v * v * e; }

// Utility: aplica deadzone e reescalona o valor para 0..1
inline float applyDeadzone(float v, float dz)
{
  if (dz <= 0.0f) return v;
  if (dz >= 0.5f) return 0.0f; // evita divisão por zero
  if (fabsf(v) <= dz) return 0.0f;
  float sign = (v > 0) ? 1.0f : -1.0f;
  float mag = (fabsf(v) - dz) / (1.0f - dz);
  return sign * mag;
}

void savePidsToNVS()
{
  preferences.begin("fc-config", false);
  preferences.putDouble("kp_roll", Kp_roll);
  preferences.putDouble("ki_roll", Ki_roll);
  preferences.putDouble("kd_roll", Kd_roll);
  preferences.putDouble("kp_pitch", Kp_pitch);
  preferences.putDouble("ki_pitch", Ki_pitch);
  preferences.putDouble("kd_pitch", Kd_pitch);
  preferences.end();
  StaticJsonDocument<64> res;
  res["status"] = "ok";
  res["msg"] = "PIDs saved";
  sendJsonSerial(res);
}

void loadPidsFromNVS()
{
  preferences.begin("fc-config", true);
  Kp_roll = preferences.getDouble("kp_roll", 1.5);
  Ki_roll = preferences.getDouble("ki_roll", 0.1);
  Kd_roll = preferences.getDouble("kd_roll", 0.05);
  Kp_pitch = preferences.getDouble("kp_pitch", 1.5);
  Ki_pitch = preferences.getDouble("ki_pitch", 0.1);
  Kd_pitch = preferences.getDouble("kd_pitch", 0.05);
  preferences.end();
}

void saveRcCalibrationToNVS()
{
  preferences.begin("fc-rc", false);
  char key[16];
  for (int i = 0; i < 8; i++)
  {
    snprintf(key, sizeof(key), "rcmin%d", i);
    preferences.putInt(key, g_rc_min[i]);
    snprintf(key, sizeof(key), "rcmax%d", i);
    preferences.putInt(key, g_rc_max[i]);
    snprintf(key, sizeof(key), "rccenter%d", i);
    preferences.putInt(key, g_rc_center[i]);
    snprintf(key, sizeof(key), "rctrim%d", i);
    preferences.putInt(key, g_rc_trim[i]);
  }
  preferences.putInt("servo_trim_l", g_servo_trim_l);
  preferences.putInt("servo_trim_r", g_servo_trim_r);
  preferences.putFloat("deadzone", g_deadzone);
  preferences.end();
  StaticJsonDocument<64> res;
  res["status"] = "ok";
  res["msg"] = "RC Cal Saved";
  sendJsonSerial(res);
}

void loadRcCalibrationFromNVS()
{
  preferences.begin("fc-rc", true);
  char key[16];
  for (int i = 0; i < 8; i++)
  {
    snprintf(key, sizeof(key), "rcmin%d", i);
    g_rc_min[i] = preferences.getInt(key, PWM_MAX);
    snprintf(key, sizeof(key), "rcmax%d", i);
    g_rc_max[i] = preferences.getInt(key, PWM_MIN);
    snprintf(key, sizeof(key), "rccenter%d", i);
    g_rc_center[i] = preferences.getInt(key, 1500);
    snprintf(key, sizeof(key), "rctrim%d", i);
    g_rc_trim[i] = preferences.getInt(key, 0);
  }
  g_servo_trim_l = preferences.getInt("servo_trim_l", 0);
  g_servo_trim_r = preferences.getInt("servo_trim_r", 0);
  g_deadzone = preferences.getFloat("deadzone", 0.05f);
  preferences.end();
}

void performGyroCalibration()
{
  bool was_in_failsafe = g_failsafe_active;
  g_failsafe_active = true;
  vTaskDelay(pdMS_TO_TICKS(100));
  StaticJsonDocument<64> info;
  info["status"] = "info";
  info["msg"] = "Calibrating Gyro...";
  sendJsonSerial(info);

  double sum_x = 0, sum_y = 0;
  sensors_event_t a, g, temp;
  for (int i = 0; i < 1000; i++)
  {
    mpu.getEvent(&a, &g, &temp);
    sum_x += g.gyro.x;
    sum_y += g.gyro.y;
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  g_gyro_roll_offset = (sum_x / 1000) * (180 / M_PI);
  g_gyro_pitch_offset = (sum_y / 1000) * (180 / M_PI);
  if (!was_in_failsafe)
    g_failsafe_active = false;

  StaticJsonDocument<64> res;
  res["status"] = "ok";
  res["msg"] = "Gyro Calibrated";
  sendJsonSerial(res);
}

void performRcCalibration(uint32_t duration_ms)
{
  bool was_in_failsafe = g_failsafe_active;
  g_failsafe_active = true;
  g_rc_calibrating = true;
  for (int i = 0; i < 8; i++)
  {
    g_rc_min[i] = 2000;
    g_rc_max[i] = 1000;
  } // Invertido intencionalmente para expandir

  StaticJsonDocument<64> info;
  info["status"] = "info";
  info["msg"] = "RC Cal Start";
  sendJsonSerial(info);

  uint32_t start = millis();
  while (millis() < start + duration_ms)
  {
    for (int i = 0; i < 8; i++)
    {
      int us = constrain(map(pkt.p[i], 0, 255, PWM_MIN, PWM_MAX), PWM_MIN, PWM_MAX);
      if (us < g_rc_min[i])
        g_rc_min[i] = us;
      if (us > g_rc_max[i])
        g_rc_max[i] = us;
    }
    delay(20);
  }
  for (int i = 0; i < 8; i++)
    g_rc_center[i] = (g_rc_min[i] + g_rc_max[i]) / 2;
  saveRcCalibrationToNVS();
  g_rc_calibrating = false;
  if (!was_in_failsafe)
    g_failsafe_active = false;
  StaticJsonDocument<64> res;
  res["status"] = "ok";
  res["msg"] = "RC Cal Done";
  sendJsonSerial(res);
}

void performCenterServos(uint32_t hold_ms)
{
  StaticJsonDocument<64> info;
  info["status"] = "info";
  info["msg"] = "Centering...";
  sendJsonSerial(info);
  int l = constrain(1500 + g_rc_trim[CH_AIL] + g_servo_trim_l, PWM_MIN, PWM_MAX);
  int r = constrain(1500 + g_rc_trim[CH_AIL] + g_servo_trim_r, PWM_MIN, PWM_MAX);
  srvL.writeMicroseconds(l);
  srvR.writeMicroseconds(r);
  esc.writeMicroseconds(PWM_IDLE);
  delay(hold_ms);
  StaticJsonDocument<64> res;
  res["status"] = "ok";
  res["msg"] = "Done Centering";
  sendJsonSerial(res);
}

void performCenterAndSave()
{
  bool hasPkt = ((millis() - g_last_radio_packet_ms) <= RX_TIMEOUT_MS) && !g_failsafe_active;
  for (int i = 0; i < 8; i++)
  {
    if (hasPkt)
      g_rc_center[i] = constrain(map(pkt.p[i], 0, 255, PWM_MIN, PWM_MAX), PWM_MIN, PWM_MAX);
    else
      g_rc_center[i] = (g_rc_center[i] >= PWM_MIN && g_rc_center[i] <= PWM_MAX) ? g_rc_center[i] : 1500;
  }
  saveRcCalibrationToNVS();
}

// =========================================================================
// === TAREFA RÁDIO ===
// =========================================================================
void taskRadioComm(void *pvParameters)
{
  Serial.println("Task Radio iniciada.");
  SPI.begin();
  if (!radio.begin())
    Serial.println("ERRO FATAL: Rádio não iniciado.");

  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_250KBPS);
  radio.setChannel(76);
  radio.setAutoAck(false);
  radio.openReadingPipe(0, RADIO_ADDRESS);
  radio.startListening();
  g_last_radio_packet_ms = millis();

  for (;;)
  {
    // 1. LER RÁDIO
    if (radio.available())
    {
      radio.read(&pkt, sizeof(pkt));
      g_last_radio_packet_ms = millis();
      g_failsafe_active = false;

      g_cmd_throttle_us = constrain(map(pkt.p[CH_THR], 0, 255, PWM_MIN, PWM_MAX), PWM_MIN, PWM_MAX);
      float E = mapByteToNorm(pkt.p[CH_ELE]);
      float A = mapByteToNorm(pkt.p[CH_AIL]);

      // Aplica deadzone antes do expo
      E = applyDeadzone(E, g_deadzone);
      A = applyDeadzone(A, g_deadzone);

      g_cmd_pitch = applyExpo(E, EXPO_E);
      g_cmd_roll = applyExpo(A, EXPO_A);

      if (pkt.s1 == 1)
        g_manual_mode = true;
      else
        g_manual_mode = false;
    }

    if (millis() - g_last_radio_packet_ms > RX_TIMEOUT_MS)
      g_failsafe_active = true;

    // 3. PARSER JSON (RPi)
    if (Serial2.available())
    {
      String cmdJson = Serial2.readStringUntil('\n');
      StaticJsonDocument<300> doc;
      DeserializationError error = deserializeJson(doc, cmdJson);

      if (!error)
      {
        const char *cmd = doc["cmd"];
        if (strcmp(cmd, "get") == 0)
        {
          const char *param = doc["param"];
          if (!param)
            param = "pids";

          if (strcmp(param, "pids") == 0)
          {
            StaticJsonDocument<256> res;
            res["status"] = "data";
            JsonObject pids = res.createNestedObject("pids");
            pids["kp_r"] = Kp_roll;
            pids["ki_r"] = Ki_roll;
            pids["kd_r"] = Kd_roll;
            pids["kp_p"] = Kp_pitch;
            pids["ki_p"] = Ki_pitch;
            pids["kd_p"] = Kd_pitch;
            sendJsonSerial(res);
          }
          else if (strcmp(param, "telemetry") == 0)
          {
            StaticJsonDocument<256> res;
            res["status"] = "data";
            res["r"] = g_telemetry_roll;
            res["p"] = g_telemetry_pitch;
            res["m"] = g_manual_mode ? "MANUAL" : "STAB";
            res["t"] = g_cmd_throttle_us;
            sendJsonSerial(res);
          }
          else if (strcmp(param, "rc") == 0)
          {
            // Retorno complexo RC (Thread Safe)
            if (xSemaphoreTake(g_serial_mutex, portMAX_DELAY))
            {
              StaticJsonDocument<512> res;
              res["status"] = "data";
              JsonObject rc = res.createNestedObject("rc");
              JsonArray channels = rc.createNestedArray("channels");
              bool hasPkt = ((millis() - g_last_radio_packet_ms) <= RX_TIMEOUT_MS) && !g_failsafe_active;
              for (int i = 0; i < 8; ++i)
              {
                channels.add(hasPkt ? constrain(map(pkt.p[i], 0, 255, PWM_MIN, PWM_MAX), PWM_MIN, PWM_MAX) : PWM_IDLE);
              }
              rc["sw1"] = hasPkt ? (pkt.s1 == 1) : false;
              rc["sw2"] = hasPkt ? (pkt.s2 == 1) : false;
              rc["failsafe"] = g_failsafe_active;
              rc["calibrating"] = g_rc_calibrating;
              // Dados de calibração
              JsonArray centers = rc.createNestedArray("centers");
              JsonArray trimArr = rc.createNestedArray("trim");
              for (int i = 0; i < 8; i++)
              {
                centers.add(g_rc_center[i]);
                trimArr.add(g_rc_trim[i]);
              }
              rc["servo_trim_l"] = g_servo_trim_l;
              rc["servo_trim_r"] = g_servo_trim_r;
              rc["deadzone"] = g_deadzone;

              serializeJson(res, Serial2);
              Serial2.println();
              xSemaphoreGive(g_serial_mutex);
            }
          }
        }
        else if (strcmp(cmd, "set") == 0)
        {
          const char *p = doc["param"];
          // Se o parâmetro for um objeto composto (rc_all), lemos campos individuais
          if (p && strcmp(p, "rc_all") == 0)
          {
            // Atualiza campos se presentes
            if (doc.containsKey("deadzone"))
            {
              g_deadzone = (float)doc["deadzone"];
              if (g_deadzone < 0) g_deadzone = 0;
              if (g_deadzone > 0.45f) g_deadzone = 0.45f;
            }
            if (doc.containsKey("servo_trim_l")) g_servo_trim_l = (int)doc["servo_trim_l"];
            if (doc.containsKey("servo_trim_r")) g_servo_trim_r = (int)doc["servo_trim_r"];
            if (doc.containsKey("trim") && doc["trim"].is<JsonArray>())
            {
              JsonArray arr = doc["trim"].as<JsonArray>();
              int i = 0;
              for (int v : arr)
              {
                if (i >= 0 && i < 8) g_rc_trim[i] = v;
                i++;
              }
            }
            saveRcCalibrationToNVS();
            StaticJsonDocument<32> res;
            res["status"] = "ok";
            res["msg"] = "rc_all saved";
            sendJsonSerial(res);
          }
          else
          {
            double v = doc["value"];

            if (strncmp(p, "kp_", 3) == 0 || strncmp(p, "ki_", 3) == 0 || strncmp(p, "kd_", 3) == 0)
            {
              xSemaphoreTake(g_pid_mutex, portMAX_DELAY);
              if (strcmp(p, "kp_roll") == 0)
                Kp_roll = v;
              else if (strcmp(p, "ki_roll") == 0)
                Ki_roll = v;
              else if (strcmp(p, "kd_roll") == 0)
                Kd_roll = v;
              if (strcmp(p, "kp_pitch") == 0)
                Kp_pitch = v;
              else if (strcmp(p, "ki_pitch") == 0)
                Ki_pitch = v;
              else if (strcmp(p, "kd_pitch") == 0)
                Kd_pitch = v;
              pid_roll.SetTunings(Kp_roll, Ki_roll, Kd_roll);
              pid_pitch.SetTunings(Kp_pitch, Ki_pitch, Kd_pitch);
              xSemaphoreGive(g_pid_mutex);
              StaticJsonDocument<32> res;
              res["status"] = "ok";
              sendJsonSerial(res);
            }
            // Trims
            else if (strncmp(p, "trim_ch", 7) == 0)
            {
              int idx = atoi(p + 7) - 1;
              if (idx >= 0 && idx < 8)
              {
                g_rc_trim[idx] = (int)v;
                saveRcCalibrationToNVS();
              }
            }
            else if (strcmp(p, "trim_servo_l") == 0)
            {
              g_servo_trim_l = (int)v;
              saveRcCalibrationToNVS();
            }
            else if (strcmp(p, "trim_servo_r") == 0)
            {
              g_servo_trim_r = (int)v;
              saveRcCalibrationToNVS();
            }
            else if (strcmp(p, "deadzone") == 0)
            {
              g_deadzone = (float)v;
              if (g_deadzone < 0) g_deadzone = 0;
              if (g_deadzone > 0.45f) g_deadzone = 0.45f;
              saveRcCalibrationToNVS();
            }
          }
        }
        else if (strcmp(cmd, "action") == 0)
        {
          if (strcmp(doc["name"], "cal_gyro") == 0)
          {
            g_request_gyro_cal = true;
          }
          else if (strcmp(doc["name"], "save_pids") == 0)
            savePidsToNVS();
          else if (strcmp(doc["name"], "cal_rc") == 0)
          {
            g_request_rc_cal = true;
          }
          else if (strcmp(doc["name"], "center_servos") == 0)
          {
            g_request_center_servos = true;
          }
          else if (strcmp(doc["name"], "center_and_save") == 0)
          {
            g_request_center_and_save = true;
          }
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void setup()
{
  Serial.begin(115200);
  Serial.println("--- BOOT ESP32 FC ---");

  xTaskCreatePinnedToCore(taskRadioComm, "TaskRadio", 4096, NULL, 1, NULL, 0);
  delay(100);

  g_pid_mutex = xSemaphoreCreateMutex();
  g_serial_mutex = xSemaphoreCreateMutex(); // Inicializa o Mutex Serial

  Serial2.begin(115200, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);
  Serial2.setTimeout(5);

  loadPidsFromNVS();
  loadRcCalibrationFromNVS();

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  esc.setPeriodHertz(50);
  srvL.setPeriodHertz(50);
  srvR.setPeriodHertz(50);
  esc.attach(PIN_ESC, PWM_MIN, PWM_MAX);
  srvL.attach(PIN_SERVO_L, PWM_MIN, PWM_MAX);
  srvR.attach(PIN_SERVO_R, PWM_MIN, PWM_MAX);
  armESC_safety();

  Wire.begin(PIN_MPU_SDA, PIN_MPU_SCL);
  if (!mpu.begin())
    Serial.println("ERRO: MPU6050");
  else
  {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    performGyroCalibration();

    // Inicializa ângulos do AHRS com leitura do acelerômetro para evitar jump inicial
    sensors_event_t a_init, g_init, temp_init;
    mpu.getEvent(&a_init, &g_init, &temp_init);
    float ax0 = a_init.acceleration.x;
    float ay0 = a_init.acceleration.y;
    float az0 = a_init.acceleration.z;
    att_roll = atan2f(ay0, az0) * RAD2DEG;
    att_pitch = atan2f(-ax0, sqrtf(ay0 * ay0 + az0 * az0)) * RAD2DEG;
  }

  pid_roll.SetOutputLimits(-400, 400);
  pid_pitch.SetOutputLimits(-400, 400);
  pid_roll.SetTunings(Kp_roll, Ki_roll, Kd_roll);
  pid_pitch.SetTunings(Kp_pitch, Ki_pitch, Kd_pitch);
  pid_roll.SetMode(AUTOMATIC);
  pid_pitch.SetMode(AUTOMATIC);
  Serial.println("Sistema Pronto.");
  last_att_us = esp_timer_get_time();
}

void loop()
{
  TickType_t xLastWakeTime = xTaskGetTickCount();
  if (g_request_gyro_cal)
  {
    performGyroCalibration();
    g_request_gyro_cal = false;
  }
  if (g_request_rc_cal)
  {
    performRcCalibration(5000);
    g_request_rc_cal = false;
  }
  if (g_request_center_servos)
  {
    performCenterServos(2000);
    g_request_center_servos = false;
  }
  if (g_request_center_and_save)
  {
    performCenterAndSave();
    g_request_center_and_save = false;
  }

  if (g_failsafe_active)
  {
    esc.writeMicroseconds(PWM_IDLE);
    srvL.writeMicroseconds(1500 + REFLEX_US);
    srvR.writeMicroseconds(1500 + REFLEX_US);
    vTaskDelayUntil(&xLastWakeTime, xLoopFrequency);
    return;
  }

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  /*
  g_input_roll = (g.gyro.x * (180/M_PI)) - g_gyro_roll_offset;
  g_input_pitch = (g.gyro.y * (180/M_PI)) - g_gyro_pitch_offset;
  */
  // Mantém entrada do PID como taxa (deg/s) - sem alteração
  g_input_roll = (g.gyro.x * RAD2DEG) - g_gyro_roll_offset;
  g_input_pitch = (g.gyro.y * RAD2DEG) - g_gyro_pitch_offset;

  // --- AHRS SIMPLES: filtro complementar para obter ângulos ---
  // tempo em segundos
  uint64_t now_us = esp_timer_get_time();
  float dt = (last_att_us == 0) ? (1.0f / LOOP_FREQ_HZ) : ((now_us - last_att_us) / 1e6f);
  last_att_us = now_us;

  // taxas do giroscópio em deg/s (já calculadas)
  float gyro_roll_rate = (g.gyro.x * RAD2DEG) - g_gyro_roll_offset;
  float gyro_pitch_rate = (g.gyro.y * RAD2DEG) - g_gyro_pitch_offset;

  // integração do giroscópio
  att_roll += gyro_roll_rate * dt;
  att_pitch += gyro_pitch_rate * dt;

  // ângulos estimados pelo acelerômetro
  float ax = a.acceleration.x;
  float ay = a.acceleration.y;
  float az = a.acceleration.z;
  float acc_roll = atan2f(ay, az) * RAD2DEG;
  float acc_pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * RAD2DEG;

  // filtro complementar: mantém giroscópio predominante, corrige com acelerômetro
  att_roll = AHRS_ALPHA * att_roll + (1.0f - AHRS_ALPHA) * acc_roll;
  att_pitch = AHRS_ALPHA * att_pitch + (1.0f - AHRS_ALPHA) * acc_pitch;

  // envia telemetria de ângulo (para o RPi / horizonte)
  g_telemetry_roll = att_roll;
  g_telemetry_pitch = att_pitch;

  // OBS: tinha uma sobrescrita acidental aqui com as taxas do giroscópio.
  // Removido para que o horizonte virtual use os ângulos estimados (att_roll/att_pitch).

  if (g_manual_mode)
  {
    float L = (-g_cmd_pitch * MANUAL_GAIN) + (g_cmd_roll * MANUAL_GAIN);
    float R = (-g_cmd_pitch * MANUAL_GAIN) - (g_cmd_roll * MANUAL_GAIN);
    auto applyDiff = [&](float x)
    { if(x<0) x*=(1.0-DIFF); return x; };
    int trimL = g_rc_trim[CH_AIL] + g_servo_trim_l;
    int trimR = g_rc_trim[CH_AIL] + g_servo_trim_r;
    srvL.writeMicroseconds(constrain(1500 + (int)applyDiff(L) + REFLEX_US + trimL, PWM_MIN, PWM_MAX));
    srvR.writeMicroseconds(constrain(1500 + (int)applyDiff(R) + REFLEX_US + trimR, PWM_MIN, PWM_MAX));
  }
  else
  {
    g_setpoint_roll = g_cmd_roll * MAX_RATE_DPS;
    g_setpoint_pitch = g_cmd_pitch * MAX_RATE_DPS;
    xSemaphoreTake(g_pid_mutex, portMAX_DELAY);
    pid_roll.Compute();
    pid_pitch.Compute();
    xSemaphoreGive(g_pid_mutex);
    float L = (-g_output_pitch * G_E) + (g_output_roll * G_A);
    float R = (-g_output_pitch * G_E) - (g_output_roll * G_A);
    auto applyDiff = [&](float x)
    { if(x<0) x*=(1.0-DIFF); return x; };
    int trimL = g_rc_trim[CH_AIL] + g_servo_trim_l;
    int trimR = g_rc_trim[CH_AIL] + g_servo_trim_r;
    srvL.writeMicroseconds(constrain(1500 + (int)applyDiff(L) + REFLEX_US + trimL, PWM_MIN, PWM_MAX));
    srvR.writeMicroseconds(constrain(1500 + (int)applyDiff(R) + REFLEX_US + trimR, PWM_MIN, PWM_MAX));
  }
  esc.writeMicroseconds(g_cmd_throttle_us);
  vTaskDelayUntil(&xLastWakeTime, xLoopFrequency);
}