#include <avr/interrupt.h>

// === MEGA 2560 — Lê 8 canais PWM do receptor e envia CSV para o PC ===
// Receptor RC comercial (ex.: Turnigy 9X) entrega pulsos de servo, não tensão analógica.
// Entradas usadas: A8..A15 (PORTK / PCINT16..23)
// Saída USB: p0,p1,p2,p3,p4,p5,p6,p7 (cada canal em 0..255)

const uint8_t CHANNEL_PINS[8] = {A8, A9, A10, A11, A12, A13, A14, A15};
const uint16_t SAMPLE_PERIOD_MS = 10;      // ~100 Hz para o PC
const uint16_t VALID_MIN_US = 750;         // rejeita ruído fora da faixa RC
const uint16_t VALID_MAX_US = 2250;
const uint32_t SIGNAL_TIMEOUT_US = 100000; // 100 ms sem pulso = canal inválido

// Ajuste aqui se algum canal do seu receptor não estiver chegando perto de 1000/2000 us.
const uint16_t CHANNEL_MIN_US[8] = {1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000};
const uint16_t CHANNEL_MAX_US[8] = {2000, 2000, 2000, 2000, 2000, 2000, 2000, 2000};
const bool CHANNEL_INVERT[8] = {false, false, false, false, false, false, false, false};

volatile uint32_t riseTimeUs[8] = {0};
volatile uint16_t pulseWidthUs[8] = {1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500};
volatile uint32_t lastPulseUs[8] = {0};
volatile uint8_t lastPortKState = 0;

uint8_t channels[8];

static inline uint8_t pulseUsToByte(uint8_t ch, uint16_t pulseUs) {
  uint16_t minUs = CHANNEL_MIN_US[ch];
  uint16_t maxUs = CHANNEL_MAX_US[ch];

  if (maxUs <= minUs) {
    maxUs = minUs + 1;
  }

  if (pulseUs < minUs) pulseUs = minUs;
  if (pulseUs > maxUs) pulseUs = maxUs;

  uint32_t scaled = (uint32_t)(pulseUs - minUs) * 255UL;
  uint8_t out = (uint8_t)((scaled + ((maxUs - minUs) / 2)) / (maxUs - minUs));

  if (CHANNEL_INVERT[ch]) {
    out = 255 - out;
  }

  return out;
}

void setupPwmCapture() {
  for (uint8_t i = 0; i < 8; i++) {
    pinMode(CHANNEL_PINS[i], INPUT);
  }

  lastPortKState = PINK;

  PCICR |= _BV(PCIE2);   // habilita pin-change para PORTK
  PCMSK2 = 0xFF;         // monitora PK0..PK7 (A8..A15)
  PCIFR |= _BV(PCIF2);   // limpa flag pendente
}

ISR(PCINT2_vect) {
  const uint8_t currentState = PINK;
  const uint8_t changed = currentState ^ lastPortKState;
  const uint32_t nowUs = micros();

  for (uint8_t i = 0; i < 8; i++) {
    const uint8_t mask = (1 << i);
    if ((changed & mask) == 0) {
      continue;
    }

    if ((currentState & mask) != 0) {
      riseTimeUs[i] = nowUs;
      continue;
    }

    const uint32_t width = nowUs - riseTimeUs[i];
    if (width >= VALID_MIN_US && width <= VALID_MAX_US) {
      pulseWidthUs[i] = (uint16_t)width;
      lastPulseUs[i] = nowUs;
    }
  }

  lastPortKState = currentState;
}

void setup() {
  Serial.begin(115200);
  setupPwmCapture();
}

void loop() {
  static uint32_t lastSampleMs = 0;
  const uint32_t nowMs = millis();
  const uint32_t nowUs = micros();

  if (nowMs - lastSampleMs < SAMPLE_PERIOD_MS) {
    return;
  }
  lastSampleMs = nowMs;

  uint16_t pulseCopy[8];
  uint32_t lastCopy[8];

  noInterrupts();
  for (uint8_t i = 0; i < 8; i++) {
    pulseCopy[i] = pulseWidthUs[i];
    lastCopy[i] = lastPulseUs[i];
  }
  interrupts();

  for (uint8_t i = 0; i < 8; i++) {
    if ((nowUs - lastCopy[i]) > SIGNAL_TIMEOUT_US) {
      channels[i] = 127;
    } else {
      channels[i] = pulseUsToByte(i, pulseCopy[i]);
    }
  }

  Serial.print(channels[0]);
  for (uint8_t i = 1; i < 8; i++) {
    Serial.print(',');
    Serial.print(channels[i]);
  }
  Serial.println();
}
