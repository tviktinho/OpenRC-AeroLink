// === RX (Arduino Nano) — Veículo RC + Saída Serial para MEGA ===
// Rádio: CE=D8, CSN=D7, 250 kbps, canal 76, AutoAck OFF, pipe 0, address "00001"

#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <Servo.h>
#include <SoftwareSerial.h>

// -------- RF --------
#define CE_PIN  8
#define CSN_PIN 7
RF24 radio(CE_PIN, CSN_PIN);
const byte ADDRESS[6] = "00001";

// -------- Pacote --------
struct Packet {
  uint8_t p[8];  // 0..255 (8 potenciômetros)
  uint8_t s1;    // 0/1
  uint8_t s2;    // 0/1
} pkt;

// -------- Saídas (veículo) --------
const uint8_t SERVO_PINS[8] = {3, 4, 5, 6, 9, 10, 14, 15}; // 14=A0, 15=A1 como digitais
const uint8_t SW1_OUT = 16; // A2 como digital
const uint8_t SW2_OUT = 17; // A3 como digital
Servo servos[8];

unsigned long lastPacketMs = 0;

// -------- Debug em porta separada --------
#define DBG_TX_PIN 4           // D4 -> RX do seu USB-TTL
SoftwareSerial DBG(255, DBG_TX_PIN);  // só TX

static inline int byteToUs(uint8_t v) { return 1000 + ((int)v * 1000) / 255; }

// ---------- envia frame binário ao MEGA (pela Serial hardware) ----------
void sendFrameToMega() {
  // 0xAA 0x55 [p0..p7] [s1] [s2] [chk XOR]
  Serial.write(0xAA); Serial.write(0x55);
  uint8_t chk = 0;
  for (uint8_t i = 0; i < 8; i++) { Serial.write(pkt.p[i]); chk ^= pkt.p[i]; }
  Serial.write(pkt.s1); chk ^= pkt.s1;
  Serial.write(pkt.s2); chk ^= pkt.s2;
  Serial.write(chk);
}

void setup() {
  // ATENÇÃO: esta Serial (USB/TX0/RX0) está ligada fisicamente no D1/D0.
  // Como D1 (TX) vai ao MEGA RX1, trate esta Serial apenas para o FRAME binário,
  // sem prints de texto (para não poluir o link).
  Serial.begin(115200);

  DBG.begin(38400);                          // debug em D4 separado
  DBG.println(F("RX pronto: 8 servos + 2 digitais + frame->MEGA (Serial)"));

  if (!radio.begin()) { while (1) {} }
  pinMode(10, OUTPUT);                       // boa prática AVR

  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_250KBPS);
  radio.setChannel(76);
  radio.setAutoAck(false);
  radio.openReadingPipe(0, ADDRESS);
  radio.startListening();

  for (uint8_t i = 0; i < 8; i++) {
    servos[i].attach(SERVO_PINS[i]);
    servos[i].writeMicroseconds(1500);
  }
  pinMode(SW1_OUT, OUTPUT);
  pinMode(SW2_OUT, OUTPUT);
  digitalWrite(SW1_OUT, LOW);
  digitalWrite(SW2_OUT, LOW);
}

void loop() {
  if (radio.available()) {
    while (radio.available()) radio.read(&pkt, sizeof(pkt));
    lastPacketMs = millis();

    // atualiza servos
    for (uint8_t i = 0; i < 8; i++) servos[i].writeMicroseconds(byteToUs(pkt.p[i]));

    // atualiza saídas digitais
    digitalWrite(SW1_OUT, pkt.s1 ? HIGH : LOW);
    digitalWrite(SW2_OUT, pkt.s2 ? HIGH : LOW);

    // envia frame pro MEGA (na Serial "limpa")
    sendFrameToMega();

    // DEBUG (em porta separada D4) a cada ~200 ms
    static uint32_t tDbg=0;
    if (millis() - tDbg >= 200) {
      tDbg = millis();
      DBG.print(F("CH: "));
      for (uint8_t i=0;i<8;i++){ DBG.print(pkt.p[i]); DBG.print(' '); }
      DBG.print(F("| S1=")); DBG.print(pkt.s1);
      DBG.print(F(" S2=")); DBG.println(pkt.s2);
    }
  }

  // Failsafe
  if (millis() - lastPacketMs > 500) {
    for (uint8_t i = 0; i < 8; i++) servos[i].writeMicroseconds(1500);
    digitalWrite(SW1_OUT, LOW);
    digitalWrite(SW2_OUT, LOW);
  }
}
