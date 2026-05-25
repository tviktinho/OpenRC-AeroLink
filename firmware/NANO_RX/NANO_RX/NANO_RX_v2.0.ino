#include <RF24.h>
#include <SPI.h>
#include <Servo.h>
#include <nRF24L01.h>

#define CE_PIN 8
#define CSN_PIN 7
const byte ADDRESS[6] = "00001";

RF24 radio(CE_PIN, CSN_PIN);

// Estrutura de dados recebida do TX Nano
struct PacketRF {
  uint8_t p[8];
  uint8_t s1;
  uint8_t s2;
} pkt;

// Pinos
const uint8_t PIN_AIL_L = 2; // D2: Aileron (Normal) ou Elevon L (Zagi)
const uint8_t PIN_ELE_R = 3; // D3: Profundor (Normal) ou Elevon R (Zagi)
const uint8_t PIN_ESC = 4;   // D4: Acelerador
const uint8_t PIN_RUD = 5;   // D5: Leme
const uint8_t PIN_AUX = 6;   // D6: Extra (Desligado)

Servo srvAilL, srvEleR, esc, srvRud, srvAux;

// Canais
const uint8_t CH_THR = 0;
const uint8_t CH_RUD = 1;
const uint8_t CH_AIL = 2;
const uint8_t CH_ELE = 3;

// Limites PWM
const int PWM_MIN = 1000;
const int PWM_MAX = 2000;
const int PWM_IDLE = 1000;

// Configs Zagi
float G_E = 1.3;
float G_A = 1.3;
float EXPO_E = 0.55;
float EXPO_A = 0.55;
float DIFF = -0.30;
int REFLEX_US = +30;

const uint32_t RX_TIMEOUT_MS = 200;
uint32_t lastRxMs = 0;

inline float mapByteToNorm(uint8_t b) { return ((int)b - 128) / 127.0f; }
inline int normToUs(float x) {
  if (x < -1)
    x = -1;
  if (x > +1)
    x = +1;
  return (int)(1500 + x * 400);
}
inline float applyExpo(float v, float e) {
  return v * (1.0f - e) + v * v * v * e;
}

void armESC_safety() {
  for (int i = 0; i < 100; ++i) {
    esc.writeMicroseconds(PWM_IDLE);
    delay(10);
  }
}

void setup() {
  esc.attach(PIN_ESC);
  srvAilL.attach(PIN_AIL_L);
  srvEleR.attach(PIN_ELE_R);
  srvRud.attach(PIN_RUD);
  // Canal extra desligado por enquanto

  esc.writeMicroseconds(PWM_IDLE);
  srvAilL.writeMicroseconds(1500);
  srvEleR.writeMicroseconds(1500);
  srvRud.writeMicroseconds(1500);

  delay(50);
  radio.begin();
  pinMode(10, OUTPUT);
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
  if (got)
    lastRxMs = millis();

  // Failsafe
  if (!got && (millis() - lastRxMs > RX_TIMEOUT_MS)) {
    esc.writeMicroseconds(PWM_IDLE);
    srvAilL.writeMicroseconds(1500 + REFLEX_US);
    srvEleR.writeMicroseconds(1500 + REFLEX_US);
    srvRud.writeMicroseconds(1500);
    return;
  }

  if (!got)
    return;

  // Swtiches do rádio (s1=Mix Zagi, s2=Corte de Motor)
  bool mix_on = (pkt.s2 == 1);
  bool motor_cut = (pkt.s1 == 1);

  // Acelerador
  int pwm_thr = map(pkt.p[CH_THR], 0, 255, PWM_MIN, PWM_MAX);
  pwm_thr = constrain(pwm_thr, PWM_MIN, PWM_MAX);
  if (motor_cut) {
    pwm_thr = PWM_IDLE;
  }

  // Leme
  int pwm_rud = map(pkt.p[CH_RUD], 0, 255, PWM_MIN, PWM_MAX);
  pwm_rud = constrain(pwm_rud, PWM_MIN, PWM_MAX);

  int usL = 1500;
  int usR = 1500;

  float E = mapByteToNorm(pkt.p[CH_ELE]);
  float A = mapByteToNorm(pkt.p[CH_AIL]);
  E = -applyExpo(E, EXPO_E) * G_E;
  A = applyExpo(A, EXPO_A) * G_A;

  if (mix_on) {
    // Modo Zagi (Mix ligado)
    float L = E - A;
    float R = E + A;

    auto withDiff = [&](float x) {
      if (x < 0)
        x *= (1.0f - DIFF);
      return x;
    };
    L = withDiff(L);
    R = withDiff(R);

    usL = normToUs(L) + REFLEX_US;
    usR = normToUs(R) + REFLEX_US;
  } else {
    // Modo Normal (Mix desligado)
    usL = normToUs(A); // D2 = Aileron
    usR = normToUs(E); // D3 = Profundor
  }

  usL = constrain(usL, PWM_MIN, PWM_MAX);
  usR = constrain(usR, PWM_MIN, PWM_MAX);

  esc.writeMicroseconds(pwm_thr);
  srvAilL.writeMicroseconds(usL);
  srvEleR.writeMicroseconds(usR);
  srvRud.writeMicroseconds(pwm_rud);
}
