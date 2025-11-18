#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <Servo.h>

#define CE_PIN 8
#define CSN_PIN 7
const byte ADDRESS[6] = "00001";

RF24 radio(CE_PIN, CSN_PIN);

// === Estruturas devem casar com o TX ===
struct PacketRF { uint8_t p[8]; uint8_t s1; uint8_t s2; } pkt;

// === Pinos ===
const uint8_t PIN_ESC   = 9;  // sinal do ESC
const uint8_t PIN_SERVO_L = 5;
const uint8_t PIN_SERVO_R = 6;

Servo esc, srvL, srvR;

// === Configs ===
const uint8_t  CH_THR = 0;    // throttle
const uint8_t  CH_ELE = 3;    // pitch
const uint8_t  CH_AIL = 2;    // roll

// Limites PWM
const int PWM_MIN = 1000;     // us
const int PWM_MAX = 2000;     // us
const int PWM_IDLE = 1000;    // motor cortado

// Throws (ganhos) para elevon
float G_E = 2.55;   // ganho de pitch
float G_A = 2.55;   // ganho de roll

// Expo (0..0.6 aprox)
float EXPO_E = 0.25;
float EXPO_A = 0.25;

// Diferencial (mais UP que DOWN)
float DIFF = -0.30;  // 30% menos para baixo

// Reflexo (neutro ligeiro para cima), em microsegundos
int REFLEX_US = +30;  // ~1–2 mm

// Failsafe
const uint32_t RX_TIMEOUT_MS = 200;
uint32_t lastRxMs = 0;

// Utilidades
inline float mapByteToNorm(uint8_t b) { return ( (int)b - 128 ) / 127.0f; }  // ~-1..+1
inline int   normToUs(float x) {
  if (x < -1) x = -1; if (x > +1) x = +1;
  return (int)(1500 + x * 400); // ±400us = throw moderado
}
inline float applyExpo(float v, float e){
  // expo padrão: out = v*(1-e) + v^3*e
  return v*(1.0f - e) + v*v*v*e;
}

void armESC_safety() {
  // Garante throttle baixo por ~1s para armar
  for (int i=0; i<100; ++i) {
    esc.writeMicroseconds(PWM_IDLE);
    delay(10);
  }
}

void setup() {
  // Servos/ESC
  esc.attach(PIN_ESC);
  srvL.attach(PIN_SERVO_L);
  srvR.attach(PIN_SERVO_R);

  esc.writeMicroseconds(PWM_IDLE);
  srvL.writeMicroseconds(1500 + REFLEX_US);
  srvR.writeMicroseconds(1500 + REFLEX_US);

  // Rádio
  delay(50);
  radio.begin();
  pinMode(10, OUTPUT); // garantir SS como saída no Nano
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_250KBPS);
  radio.setChannel(76);
  radio.setAutoAck(false);
  radio.openReadingPipe(0, ADDRESS);
  radio.startListening();

  armESC_safety();
  lastRxMs = millis();
}

void loop() {
  bool got = false;
  while (radio.available()) {
    radio.read(&pkt, sizeof(pkt));
    got = true;
  }
  if (got) lastRxMs = millis();

  if (!got && (millis() - lastRxMs > RX_TIMEOUT_MS)) {
    // === FAILSAFE ===
    esc.writeMicroseconds(PWM_IDLE);
    srvL.writeMicroseconds(1500 + REFLEX_US);
    srvR.writeMicroseconds(1500 + REFLEX_US);
    return;
  }

  if (!got) return; // nada novo

  // === Lê canais do pacote ===
  // throttle 0..255 -> 1000..2000us (curva linear com "deadzone" baixa opcional)
  int pwm_thr = map(pkt.p[CH_THR], 0, 255, PWM_MIN, PWM_MAX);
  // Segurança: não deixa passar acima do máx
  if (pwm_thr < PWM_MIN) pwm_thr = PWM_MIN;
  if (pwm_thr > PWM_MAX) pwm_thr = PWM_MAX;

  // E/A normalizados -1..+1 com expo
  float E = mapByteToNorm(pkt.p[CH_ELE]);
  float A = mapByteToNorm(pkt.p[CH_AIL]);
  E = applyExpo(E, EXPO_E) * G_E;
  A = applyExpo(A, EXPO_A) * G_A;

  // Mistura elevon
  float L = E - A;
  float R = E + A;

  // Aplica diferencial (menos deflexão para baixo)
  auto withDiff = [&](float x){
    // x>0 -> acima (UP), x<0 -> abaixo (DOWN)
    if (x < 0) x *= (1.0f - DIFF);
    return x;
  };
  L = withDiff(L);
  R = withDiff(R);

  // Converte para µs
  int usL = normToUs(L) + REFLEX_US;
  int usR = normToUs(R) + REFLEX_US;

  // Satura 1000..2000
  usL = constrain(usL, PWM_MIN, PWM_MAX);
  usR = constrain(usR, PWM_MIN, PWM_MAX);

  // Saídas
  esc.writeMicroseconds(pwm_thr);
  srvL.writeMicroseconds(usL);
  srvR.writeMicroseconds(usR);
}
