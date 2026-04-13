// === MEGA 2560 — Lê 8 canais PWM (pinos 2..9) e envia CSV para o PC ===

struct Frame {
  uint8_t p[8];
  uint8_t s1;
  uint8_t s2;
} f;

const uint8_t PWM_PINS[8] = {2, 3, 4, 5, 6, 7, 8, 9};
const unsigned long PWM_TIMEOUT_US = 3000UL;
const uint16_t PWM_MIN_US = 1000;
const uint16_t PWM_MAX_US = 2000;
const uint16_t PWM_VALID_MIN_US = 900;
const uint16_t PWM_VALID_MAX_US = 2200;
const unsigned long PRINT_PERIOD_MS = 20;

uint8_t readPwmAsByte(uint8_t pin, uint8_t fallback) {
  unsigned long pulse = pulseIn(pin, HIGH, PWM_TIMEOUT_US);
  if (pulse < PWM_VALID_MIN_US || pulse > PWM_VALID_MAX_US) return fallback;
  long mapped = map((long)pulse, PWM_MIN_US, PWM_MAX_US, 0, 255);
  if (mapped < 0) mapped = 0;
  if (mapped > 255) mapped = 255;
  return (uint8_t)mapped;
}

void readPwmInputs() {
  for (uint8_t i = 0; i < 8; i++) {
    f.p[i] = readPwmAsByte(PWM_PINS[i], f.p[i]);
  }

  // Mantém o mesmo CSV esperado no PC: p0..p7,s1,s2
  // s1/s2 são limiares dos canais 6 e 7 (índices 6 e 7), preservando
  // compatibilidade com o software PC que espera dois switches separados.
  f.s1 = (f.p[6] >= 128) ? 255 : 0;
  f.s2 = (f.p[7] >= 128) ? 255 : 0;
}

void setup() {
  Serial.begin(115200);  // USB -> PC (CSV)
  for (uint8_t i = 0; i < 8; i++) {
    pinMode(PWM_PINS[i], INPUT);
    f.p[i] = 127;
  }
  f.s1 = 0;
  f.s2 = 0;
  Serial.println(F("# MEGA pronto: lendo PWM nos pinos 2..9..."));
}

void loop() {
  static unsigned long lastPrint = 0;
  readPwmInputs();

  if (millis() - lastPrint >= PRINT_PERIOD_MS) {
    lastPrint = millis();
    Serial.print(f.p[0]);
    for (int i = 1; i < 8; i++) { Serial.print(','); Serial.print(f.p[i]); }
    Serial.print(','); Serial.print(f.s1);
    Serial.print(','); Serial.print(f.s2);
    Serial.println();
  }
}
