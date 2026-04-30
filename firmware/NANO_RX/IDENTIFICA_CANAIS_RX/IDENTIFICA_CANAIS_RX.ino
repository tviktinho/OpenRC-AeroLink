// Diagnostico RX nRF24.
// Abra Serial Monitor em 115200.
// Mova um comando por vez.

#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

#define CE_PIN 8
#define CSN_PIN 7

RF24 radio(CE_PIN, CSN_PIN);
const byte ADDRESS[6] = "00001";

struct Packet {
  uint8_t p[8];
  uint8_t s1;
  uint8_t s2;
} pkt;

uint8_t lastP[8] = {0};
uint8_t lastS1 = 0;
uint8_t lastS2 = 0;
bool haveBaseline = false;
unsigned long lastPrintMs = 0;

int centered(uint8_t v) {
  return (int)v - 128;
}

void printAllChannels() {
  Serial.print(F("RAW "));
  for (uint8_t i = 0; i < 8; i++) {
    Serial.print(F("p["));
    Serial.print(i);
    Serial.print(F("]="));
    Serial.print(pkt.p[i]);
    Serial.print(F("("));
    Serial.print(centered(pkt.p[i]));
    Serial.print(F(") "));
  }
  Serial.print(F("s1="));
  Serial.print(pkt.s1);
  Serial.print(F(" s2="));
  Serial.println(pkt.s2);
}

void printChangedChannels() {
  for (uint8_t i = 0; i < 8; i++) {
    int delta = (int)pkt.p[i] - (int)lastP[i];
    if (delta > 8 || delta < -8) {
      Serial.print(F("MEXEU: p["));
      Serial.print(i);
      Serial.print(F("] valor="));
      Serial.print(pkt.p[i]);
      Serial.print(F(" delta="));
      Serial.println(delta);
    }
  }

  if (pkt.s1 != lastS1) {
    Serial.print(F("MEXEU: s1="));
    Serial.println(pkt.s1);
  }

  if (pkt.s2 != lastS2) {
    Serial.print(F("MEXEU: s2="));
    Serial.println(pkt.s2);
  }
}

void saveBaseline() {
  for (uint8_t i = 0; i < 8; i++) {
    lastP[i] = pkt.p[i];
  }
  lastS1 = pkt.s1;
  lastS2 = pkt.s2;
  haveBaseline = true;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  Serial.println(F("Identificador canais RX"));
  Serial.println(F("Mova um stick/chave por vez"));
  Serial.println(F("Anote indice p[x] mostrado"));

  if (!radio.begin()) {
    Serial.println(F("ERRO radio.begin"));
    while (1) {}
  }

  pinMode(10, OUTPUT);
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_250KBPS);
  radio.setChannel(76);
  radio.setAutoAck(false);
  radio.openReadingPipe(0, ADDRESS);
  radio.startListening();
}

void loop() {
  if (!radio.available()) {
    if (millis() - lastPrintMs > 1000) {
      lastPrintMs = millis();
      Serial.println(F("Sem pacote RF"));
    }
    return;
  }

  while (radio.available()) {
    radio.read(&pkt, sizeof(pkt));
  }

  if (!haveBaseline) {
    saveBaseline();
    printAllChannels();
    return;
  }

  printChangedChannels();

  if (millis() - lastPrintMs > 500) {
    lastPrintMs = millis();
    printAllChannels();
  }

  saveBaseline();
}
