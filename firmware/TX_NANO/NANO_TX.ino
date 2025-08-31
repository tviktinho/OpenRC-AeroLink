/* NANO_TX COMPATÍVEL
 * RF: CE=D8, CSN=D7, canal 76, 250 kbps, AutoAck OFF, PA_LOW (igual ao seu)
 * Entradas: 8 pots (A0..A7), SW1=D3, SW2=D4, CAL=D6 (INPUT_PULLUP)
 * RF Packet: struct Packet { uint8_t p[8]; uint8_t s1; uint8_t s2; }  // igual ao seu
 * UART ESPELHO (115200): frame AA 55 | len | payload(11B) | crc8(0x8C)
 *   payload UART = { ch[8], sw(bits:0=S1,1=S2,2=CAL), rfu0=0, rfu1=0 }
 */

#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

// -------- RF (igual ao seu) --------
#define CE_PIN   8   // D8
#define CSN_PIN  7   // D7
RF24 radio(CE_PIN, CSN_PIN);
const byte ADDRESS[6] = "00001";

// -------- Pinos de entrada --------
const uint8_t potPins[8] = {A0,A1,A2,A3,A4,A5,A6,A7};
#define SW1_PIN       3
#define SW2_PIN       4
#define CALIB_BTN_PIN 6  // INPUT_PULLUP (LOW = pressionado)

// -------- Pacote RF (igual ao seu) --------
struct Packet {
  uint8_t p[8];  // 8 canais 0..255
  uint8_t s1;    // 0/1
  uint8_t s2;    // 0/1
} pkt;

// -------- Payload UART (para HUDs) --------
struct __attribute__((packed)) PayloadUART {
  uint8_t ch[8];
  uint8_t sw;    // bit0=S1, bit1=S2, bit2=CAL
  uint8_t rfu0;  // 0
  uint8_t rfu1;  // 0
} upkt;

// -------- Calibração/Suavização (igual ao seu) --------
struct Calib { uint16_t minRaw[8]; uint16_t maxRaw[8]; } calib;
const uint8_t SMOOTHING = 2; // 0..10
uint8_t smoothVal[8] = {0};
unsigned long lastSend = 0, lastPrint = 0;
bool calibrating = false;

// -------- Utilidades --------
static bool initRadioLikeYours() {
  delay(50);
  if (!radio.begin()) return false;

  pinMode(10, OUTPUT); // manter SS em OUTPUT (boa prática AVR)

  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_250KBPS);
  radio.setChannel(76);
  radio.setAutoAck(false);
  radio.openWritingPipe(ADDRESS);
  radio.stopListening();
  return radio.isChipConnected();
}

inline void resetCalibrationBounds() {
  for (uint8_t i=0;i<8;i++){ calib.minRaw[i]=1023; calib.maxRaw[i]=0; }
}
inline void finalizeCalibrationBounds() {
  for (uint8_t i=0;i<8;i++){
    if (calib.maxRaw[i] <= calib.minRaw[i] || (calib.maxRaw[i]-calib.minRaw[i] < 8)) {
      calib.minRaw[i]=0; calib.maxRaw[i]=1023;
    }
  }
}
inline void updateCalibrationBoundsOnce() {
  for (uint8_t i=0;i<8;i++){
    int raw = analogRead(potPins[i]);
    if (raw < calib.minRaw[i]) calib.minRaw[i] = raw;
    if (raw > calib.maxRaw[i]) calib.maxRaw[i] = raw;
  }
}
static inline uint8_t normalizeToByte(int raw, uint16_t mn, uint16_t mx) {
  if (mx <= mn) { mn=0; mx=1023; }
  if (raw < (int)mn) raw = mn;
  if (raw > (int)mx) raw = mx;
  return (uint8_t)map(raw, mn, mx, 0, 255);
}

// CRC8 p/ frame UART
static uint8_t crc8(const uint8_t* d, size_t n) {
  uint8_t c=0x00;
  for (size_t i=0;i<n;i++){
    uint8_t x=d[i];
    for (uint8_t b=0;b<8;b++){
      uint8_t mix=(c^x)&1; c>>=1; if(mix) c^=0x8C; x>>=1;
    }
  }
  return c;
}

void setup() {
  Serial.begin(115200);

  pinMode(SW1_PIN, INPUT_PULLUP);
  pinMode(SW2_PIN, INPUT_PULLUP);
  pinMode(CALIB_BTN_PIN, INPUT_PULLUP);

  bool ok=false;
  for (uint8_t i=0;i<5 && !ok;i++){ ok=initRadioLikeYours(); if(!ok) delay(100); }
  if (!ok) { while(1){ /* falha no rádio */ } }

  resetCalibrationBounds();

  for (uint8_t i=0;i<8;i++){
    int raw = analogRead(potPins[i]);
    smoothVal[i] = normalizeToByte(raw, 0, 1023);
  }

  calibrating = (digitalRead(CALIB_BTN_PIN) == LOW);
}

void loop() {
  const unsigned long now = millis();
  const bool btnPressed = (digitalRead(CALIB_BTN_PIN) == LOW);

  if (btnPressed && !calibrating) {
    calibrating = true;
    resetCalibrationBounds();
  } else if (!btnPressed && calibrating) {
    calibrating = false;
    finalizeCalibrationBounds();
  }
  if (calibrating) updateCalibrationBoundsOnce();

  // 25 ms ~ 40 Hz (pode deixar 20 ms ~ 50 Hz se quiser)
  if (now - lastSend >= 25) {
    lastSend = now;

    // Atualiza canais com calibração + suavização (igual ao seu)
    for (uint8_t i=0;i<8;i++){
      int raw = analogRead(potPins[i]);
      uint16_t mn = calibrating ? 0 : calib.minRaw[i];
      uint16_t mx = calibrating ? 1023 : calib.maxRaw[i];
      uint8_t nrm = normalizeToByte(raw, mn, mx);
      smoothVal[i] = smoothVal[i] + (int)(nrm - smoothVal[i]) / (SMOOTHING + 1);
      pkt.p[i] = smoothVal[i];
    }

    pkt.s1 = (digitalRead(SW1_PIN) == LOW) ? 1 : 0;
    pkt.s2 = (digitalRead(SW2_PIN) == LOW) ? 1 : 0;

    // --- RF: exatamente seu Packet/config ---
    radio.write(&pkt, sizeof(pkt));

    // --- UART: frame para HUDs (converte s1/s2->bits e inclui CAL) ---
    for (uint8_t i=0;i<8;i++) upkt.ch[i] = pkt.p[i];
    const uint8_t cal = (digitalRead(CALIB_BTN_PIN) == LOW) ? 1 : 0;
    upkt.sw   = (pkt.s1 ? 1 : 0) | ((pkt.s2 ? 1 : 0) << 1) | (cal << 2);
    upkt.rfu0 = 0; upkt.rfu1 = 0;

    const uint8_t *pay = reinterpret_cast<const uint8_t*>(&upkt);
    const uint8_t  LEN = sizeof(PayloadUART);
    const uint8_t  CRC = crc8(pay, LEN);

    Serial.write(0xAA); Serial.write(0x55); Serial.write(LEN);
    Serial.write(pay, LEN); Serial.write(CRC);
  }

  // (Opcional) telemetria de debug a cada 50 ms
  if (now - lastPrint >= 50) {
    lastPrint = now;
    // descomente se precisar:
    for (uint8_t i=0;i<8;i++){ Serial.print(pkt.p[i]); Serial.print(' '); }
    Serial.print(pkt.s1); Serial.print(' '); Serial.print(pkt.s2); Serial.println();
  }
}
