/**
 * FIRMWARE DE VOO AIO (FC + RX) PARA ESP32 DUAL-CORE
 * COMPATÍVEL COM NANO_TX CUSTOMIZADO
 *
 * ARQUITETURA:
 * - CORE 1 (Loop): Voo (Estabilizado ou Manual), Leitura MPU, Mixagem.
 * - CORE 0 (Task): Rádio nRF24, Failsafe e Ponte API com RPi.
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
 const uint8_t PIN_ESC     = 12; 
 const uint8_t PIN_SERVO_L = 13; 
 const uint8_t PIN_SERVO_R = 14; 
 
 const uint8_t PIN_NRF_CE  = 4;
 const uint8_t PIN_NRF_CSN = 5;
 const uint8_t PIN_MPU_SDA = 21;
 const uint8_t PIN_MPU_SCL = 22;
 
 // UART RASPBERRY PI (Pinos Seguros)
 const uint8_t PIN_UART_RX = 26; 
 const uint8_t PIN_UART_TX = 27; 
 
 // === OBJETOS ===
 RF24 radio(PIN_NRF_CE, PIN_NRF_CSN);
 Servo esc, srvL, srvR;
 Adafruit_MPU6050 mpu;
 Preferences preferences; 
 SemaphoreHandle_t g_pid_mutex; 
 
 // === CONFIGS VOO ===
 const int PWM_MIN = 1000, PWM_MAX = 2000, PWM_IDLE = 1000;
 float G_E = 1.0, G_A = 1.0; 
 float EXPO_E = 0.5, EXPO_A = 0.5, DIFF = 0.50;
 int REFLEX_US = -30;
 const float MAX_RATE_DPS = 300; 
 const float MANUAL_GAIN = 300.0; 
 
 // === RÁDIO ===
 const byte RADIO_ADDRESS[6] = "00001";
 
 // Estrutura exata do seu Transmissor Nano
 struct __attribute__((packed)) PacketRF { 
   uint8_t p[8]; // Pots A0-A7
   uint8_t s1;   // Switch 1
   uint8_t s2;   // Switch 2
 } pkt;
 
 // Mapeamento baseado no seu código Nano:
 // A0=Thr, A2=Ail (Roll), A3=Ele (Pitch) - (Ajuste se necessário)
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
 volatile bool g_request_gyro_cal = false; 
 
 volatile float g_telemetry_roll = 0.0;
 volatile float g_telemetry_pitch = 0.0;
 
 double g_setpoint_roll, g_input_roll, g_output_roll;
 double g_setpoint_pitch, g_input_pitch, g_output_pitch;
 volatile double g_gyro_roll_offset = 0.0;
 volatile double g_gyro_pitch_offset = 0.0;
 
 PID pid_roll(&g_input_roll, &g_output_roll, &g_setpoint_roll, Kp_roll, Ki_roll, Kd_roll, DIRECT);
 PID pid_pitch(&g_input_pitch, &g_output_pitch, &g_setpoint_pitch, Kp_pitch, Ki_pitch, Kd_pitch, DIRECT);
 
 // =========================================================================
 // === FUNÇÕES AUXILIARES ===
 // =========================================================================
 void armESC_safety() {
   for (int i=0; i<100; ++i) { esc.writeMicroseconds(PWM_IDLE); delay(10); }
 }
 inline float mapByteToNorm(uint8_t b) { return ( (int)b - 128 ) / 127.0f; }
 inline float applyExpo(float v, float e) { return v*(1.0f - e) + v*v*v*e; }
 
 void savePidsToNVS() {
   preferences.begin("fc-config", false);
   preferences.putDouble("kp_roll", Kp_roll); preferences.putDouble("ki_roll", Ki_roll); preferences.putDouble("kd_roll", Kd_roll);
   preferences.putDouble("kp_pitch", Kp_pitch); preferences.putDouble("ki_pitch", Ki_pitch); preferences.putDouble("kd_pitch", Kd_pitch);
   preferences.end();
   Serial2.println("{\"status\":\"ok\", \"msg\":\"PIDs saved\"}");
 }
 
 void loadPidsFromNVS() {
   preferences.begin("fc-config", true);
   Kp_roll = preferences.getDouble("kp_roll", 1.5); Ki_roll = preferences.getDouble("ki_roll", 0.1); Kd_roll = preferences.getDouble("kd_roll", 0.05);
   Kp_pitch = preferences.getDouble("kp_pitch", 1.5); Ki_pitch = preferences.getDouble("ki_pitch", 0.1); Kd_pitch = preferences.getDouble("kd_pitch", 0.05);
   preferences.end();
   Serial.println("PIDs carregados.");
 }
 
 void performGyroCalibration() {
   bool was_in_failsafe = g_failsafe_active;
   g_failsafe_active = true; 
   vTaskDelay(pdMS_TO_TICKS(100));
   Serial2.println("{\"status\":\"info\", \"msg\":\"Calibrating...\"}");
   double sum_x = 0, sum_y = 0;
   sensors_event_t a, g, temp;
   for (int i = 0; i < 1000; i++) {
     mpu.getEvent(&a, &g, &temp);
     sum_x += g.gyro.x; sum_y += g.gyro.y;
     vTaskDelay(pdMS_TO_TICKS(2));
   }
   g_gyro_roll_offset = (sum_x / 1000) * (180/M_PI);
   g_gyro_pitch_offset = (sum_y / 1000) * (180/M_PI);
   if (!was_in_failsafe) g_failsafe_active = false;
   Serial2.println("{\"status\":\"ok\", \"msg\":\"Calibrated\"}");
   Serial.printf("Gyro Offsets: %.2f, %.2f\n", g_gyro_roll_offset, g_gyro_pitch_offset);
 }
 
 // =========================================================================
 // === TAREFA RÁDIO ===
 // =========================================================================
 void taskRadioComm(void *pvParameters) {
   Serial.println("Task Radio iniciada.");
   SPI.begin();
   if (!radio.begin()) Serial.println("ERRO FATAL: Rádio não iniciado.");
   
   radio.setPALevel(RF24_PA_LOW);
   radio.setDataRate(RF24_250KBPS);
   radio.setChannel(76);
   radio.setAutoAck(false);
   radio.openReadingPipe(0, RADIO_ADDRESS);
   radio.startListening();
   g_last_radio_packet_ms = millis();
   
   for (;;) {
     // 1. LER RÁDIO
     if (radio.available()) {
       radio.read(&pkt, sizeof(pkt));
       g_last_radio_packet_ms = millis();
       g_failsafe_active = false;
 
       g_cmd_throttle_us = constrain(map(pkt.p[CH_THR], 0, 255, PWM_MIN, PWM_MAX), PWM_MIN, PWM_MAX);
       float E = mapByteToNorm(pkt.p[CH_ELE]);
       float A = mapByteToNorm(pkt.p[CH_AIL]);
       g_cmd_pitch = applyExpo(E, EXPO_E);
       g_cmd_roll  = applyExpo(A, EXPO_A);
 
       // --- AJUSTE PARA O SEU CONTROLE NANO ---
       // O Nano envia pkt.s1 = 1 se pressionado, 0 se solto
       // Usamos s1 para ativar o modo manual
       if (pkt.s1 == 1) { 
         g_manual_mode = true; 
       } else {
         g_manual_mode = false;
       }
     }
 
     if (millis() - g_last_radio_packet_ms > RX_TIMEOUT_MS) g_failsafe_active = true;
 
     // 3. PARSER JSON (RPi)
     if (Serial2.available()) { 
       String cmdJson = Serial2.readStringUntil('\n'); 
       StaticJsonDocument<300> doc;
       DeserializationError error = deserializeJson(doc, cmdJson);
 
       if (!error) {
         const char* cmd = doc["cmd"];
         if (strcmp(cmd, "get") == 0) {
            StaticJsonDocument<256> res; res["status"] = "data";
            JsonObject pids = res.createNestedObject("pids");
            pids["kp_r"] = Kp_roll; pids["ki_r"] = Ki_roll; pids["kd_r"] = Kd_roll;
            pids["kp_p"] = Kp_pitch; pids["ki_p"] = Ki_pitch; pids["kd_p"] = Kd_pitch;
            serializeJson(res, Serial2); Serial2.println();
         } else if (strcmp(cmd, "telemetry") == 0) {
            StaticJsonDocument<256> res; res["status"] = "data";
            res["r"] = g_telemetry_roll; res["p"] = g_telemetry_pitch;
            res["m"] = g_manual_mode ? "MANUAL" : "STAB"; res["t"] = g_cmd_throttle_us;
            serializeJson(res, Serial2); Serial2.println();
         } else if (strcmp(cmd, "set") == 0) {
            const char* p = doc["param"]; double v = doc["value"];
            xSemaphoreTake(g_pid_mutex, portMAX_DELAY);
            if(strcmp(p,"kp_roll")==0) Kp_roll=v; else if(strcmp(p,"ki_roll")==0) Ki_roll=v; else if(strcmp(p,"kd_roll")==0) Kd_roll=v;
            if(strcmp(p,"kp_pitch")==0) Kp_pitch=v; else if(strcmp(p,"ki_pitch")==0) Ki_pitch=v; else if(strcmp(p,"kd_pitch")==0) Kd_pitch=v;
            pid_roll.SetTunings(Kp_roll, Ki_roll, Kd_roll); pid_pitch.SetTunings(Kp_pitch, Ki_pitch, Kd_pitch);
            xSemaphoreGive(g_pid_mutex);
            Serial2.println("{\"status\":\"ok\"}");
         } else if (strcmp(cmd, "action") == 0) {
            if(strcmp(doc["name"], "cal_gyro")==0) { g_request_gyro_cal = true; Serial2.println("{\"status\":\"info\",\"msg\":\"Req Cal\"}"); }
            else if(strcmp(doc["name"], "save_pids")==0) savePidsToNVS();
         }
       }
     }
     vTaskDelay(pdMS_TO_TICKS(5));
   }
 }
 
 void setup() {
   Serial.begin(115200); Serial.println("--- BOOT ESP32 FC ---");
   
   xTaskCreatePinnedToCore(taskRadioComm, "TaskRadio", 4096, NULL, 1, NULL, 0);
   delay(100);
 
   g_pid_mutex = xSemaphoreCreateMutex();
   Serial2.begin(115200, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);
   Serial2.setTimeout(5); 
 
   loadPidsFromNVS(); 
   ESP32PWM::allocateTimer(0); ESP32PWM::allocateTimer(1); ESP32PWM::allocateTimer(2);
   esc.setPeriodHertz(50); srvL.setPeriodHertz(50); srvR.setPeriodHertz(50);
   esc.attach(PIN_ESC, PWM_MIN, PWM_MAX);
   srvL.attach(PIN_SERVO_L, PWM_MIN, PWM_MAX);
   srvR.attach(PIN_SERVO_R, PWM_MIN, PWM_MAX);
   armESC_safety();
 
   Wire.begin(PIN_MPU_SDA, PIN_MPU_SCL);
   if (!mpu.begin()) Serial.println("ERRO: MPU6050");
   else {
     mpu.setAccelerometerRange(MPU6050_RANGE_8_G); mpu.setGyroRange(MPU6050_RANGE_500_DEG);
     performGyroCalibration(); 
   }
 
   pid_roll.SetOutputLimits(-400, 400); pid_pitch.SetOutputLimits(-400, 400);
   pid_roll.SetTunings(Kp_roll, Ki_roll, Kd_roll); pid_pitch.SetTunings(Kp_pitch, Ki_pitch, Kd_pitch);
   pid_roll.SetMode(AUTOMATIC); pid_pitch.SetMode(AUTOMATIC);
   Serial.println("Sistema Pronto.");
 }
 
 void loop() {
   TickType_t xLastWakeTime = xTaskGetTickCount();
   if (g_request_gyro_cal) { performGyroCalibration(); g_request_gyro_cal = false; }
   if (g_failsafe_active) {
     esc.writeMicroseconds(PWM_IDLE); srvL.writeMicroseconds(1500 + REFLEX_US); srvR.writeMicroseconds(1500 + REFLEX_US);
     vTaskDelayUntil(&xLastWakeTime, xLoopFrequency); return;
   }
 
   sensors_event_t a, g, temp;
   mpu.getEvent(&a, &g, &temp);
   float gyro_roll  = (g.gyro.x * (180/M_PI)) - g_gyro_roll_offset;
   float gyro_pitch = (g.gyro.y * (180/M_PI)) - g_gyro_pitch_offset;
   
   g_input_roll = gyro_roll; g_input_pitch = gyro_pitch;
   g_telemetry_roll = gyro_roll; g_telemetry_pitch = gyro_pitch;
 
   if (g_manual_mode) {
       // MODO MANUAL (ACRO)
       float L = (-g_cmd_pitch * MANUAL_GAIN) + (g_cmd_roll * MANUAL_GAIN);
       float R = (-g_cmd_pitch * MANUAL_GAIN) - (g_cmd_roll * MANUAL_GAIN);
       auto applyDiff = [&](float x){ if(x<0) x*=(1.0-DIFF); return x; };
       srvL.writeMicroseconds(constrain(1500 + (int)applyDiff(L) + REFLEX_US, PWM_MIN, PWM_MAX));
       srvR.writeMicroseconds(constrain(1500 + (int)applyDiff(R) + REFLEX_US, PWM_MIN, PWM_MAX));
   } else {
       // MODO ESTABILIZADO
       g_setpoint_roll  = g_cmd_roll * MAX_RATE_DPS;
       g_setpoint_pitch = g_cmd_pitch * MAX_RATE_DPS;
       xSemaphoreTake(g_pid_mutex, portMAX_DELAY);
       pid_roll.Compute(); pid_pitch.Compute();
       xSemaphoreGive(g_pid_mutex);
       float L = (-g_output_pitch * G_E) + (g_output_roll * G_A);
       float R = (-g_output_pitch * G_E) - (g_output_roll * G_A);
       auto applyDiff = [&](float x){ if(x<0) x*=(1.0-DIFF); return x; };
       srvL.writeMicroseconds(constrain(1500 + (int)applyDiff(L) + REFLEX_US, PWM_MIN, PWM_MAX));
       srvR.writeMicroseconds(constrain(1500 + (int)applyDiff(R) + REFLEX_US, PWM_MIN, PWM_MAX));
   }
   esc.writeMicroseconds(g_cmd_throttle_us);
   vTaskDelayUntil(&xLastWakeTime, xLoopFrequency);
 }