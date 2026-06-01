// =============================================================================
// ESP32C3_RX_PPM — Receptor nRF24 V2 → PPM single-wire para FC comercial
// Plataforma: ESP32-C3 Mini.
// Parte do OpenRC-AeroLink — hobby (não TCC).
// =============================================================================
// CONTRATO V2 (compatível com NANO_RX_v2.0.ino e 14-BRIZA.ino):
//   nRF24: address "00001", channel 76, 250 kbps, AutoAck OFF, payload 10 bytes
//   struct PacketRF { uint8_t p[8]; uint8_t s1; uint8_t s2; }
//   canais: p[0]=THR, p[1]=RUD, p[2]=AIL, p[3]=ELE, p[4..7]=aux
//
// PPM POSITIVO (single-wire, 1 fio + GND)
//   8 canais. Idle = LOW.
//   Cada canal: pulso HIGH 300 us + LOW (700..1700 us) → total 1000..2000 us
//   Sync gap após último canal: LOW por ~8 ms
//   Frame total ~20 ms (~50 Hz)
//
// PINAGEM
//   nRF24 SPI: SCK=4, MISO=5, MOSI=6, CSN=7, CE=10  (3.3V + cap 10–100uF)
//   PPM out:   GPIO21 → pad PPM da FC (geralmente PA03 do STM32F411)
//   GND:       comum entre ESP, nRF24 e FC (obrigatório)
// =============================================================================

#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include "driver/gpio.h"
#include "driver/rmt.h"

// =============================================================================
// PINAGEM
// =============================================================================
static constexpr int PIN_NRF_CE   = 10;
static constexpr int PIN_NRF_CSN  = 7;
static constexpr int PIN_SPI_SCK  = 4;
static constexpr int PIN_SPI_MISO = 5;
static constexpr int PIN_SPI_MOSI = 6;

static constexpr int PIN_PPM_TX   = 21;

// =============================================================================
// CONFIG RÁDIO
// =============================================================================
static const byte    RF_ADDRESS[6] = "00001";
static constexpr uint8_t RF_CHANNEL = 76;

// =============================================================================
// CONFIG PPM
// =============================================================================
static constexpr uint8_t  PPM_NUM_CHANNELS = 8;
static constexpr uint16_t PPM_PULSE_HIGH_US = 300;   // pulso start de cada canal
static constexpr uint16_t PPM_CHANNEL_MIN_US = 1000; // 1000us = stick low
static constexpr uint16_t PPM_CHANNEL_MID_US = 1500;
static constexpr uint16_t PPM_CHANNEL_MAX_US = 2000;
static constexpr uint16_t PPM_SYNC_GAP_US   = 8000;  // ~8ms sync entre frames
static constexpr uint32_t RX_TIMEOUT_MS     = 200;

// =============================================================================
// ESTRUTURA RX
// =============================================================================
struct __attribute__((packed)) PacketRF {
  uint8_t p[8];
  uint8_t s1;
  uint8_t s2;
};
static PacketRF pkt;

// =============================================================================
// ESTADO
// =============================================================================
static RF24 radio(PIN_NRF_CE, PIN_NRF_CSN);

static uint16_t ppm_us[PPM_NUM_CHANNELS] = {1500,1500,1000,1500,1000,1500,1500,1500};
static uint32_t lastRxMs = 0;
static uint32_t lastPpmUs = 0;
static uint32_t rxCount = 0;
static uint32_t failsafeFrames = 0;

// =============================================================================
// UTILS
// =============================================================================

// 0..255 (byte do TX V2) → 1000..2000us (PPM). Center 128→1500.
static inline uint16_t byteToPpm(uint8_t b) {
  return PPM_CHANNEL_MIN_US +
         ((uint32_t)b * (PPM_CHANNEL_MAX_US - PPM_CHANNEL_MIN_US) + 127) / 255;
}

static inline uint16_t boolToPpm(uint8_t v) {
  return v ? PPM_CHANNEL_MAX_US : PPM_CHANNEL_MIN_US;
}

// =============================================================================
// MAPEAMENTO V2 → 8 canais PPM (padrão AETR + 4 aux)
// =============================================================================
static void mapPacketToPpm(const PacketRF& p) {
  ppm_us[0] = byteToPpm(p.p[3]);   // CH1 Roll  ← ELE (trocado com Pitch)
  ppm_us[1] = byteToPpm(p.p[2]);   // CH2 Pitch ← AIL (trocado com Roll)
  ppm_us[2] = byteToPpm(p.p[0]);   // CH3 Throttle
  ppm_us[3] = byteToPpm(p.p[1]);   // CH4 Yaw   ← RUD
  ppm_us[4] = boolToPpm(p.s1);     // CH5 AUX1  ← s1 (arm)
  ppm_us[5] = boolToPpm(p.s2);     // CH6 AUX2  ← s2 (mode)
  ppm_us[6] = byteToPpm(p.p[4]);   // CH7 AUX3
  ppm_us[7] = byteToPpm(p.p[5]);   // CH8 AUX4
}

static void setFailsafePpm() {
  ppm_us[0] = PPM_CHANNEL_MID_US;          // Roll  centro
  ppm_us[1] = PPM_CHANNEL_MID_US;          // Pitch centro
  ppm_us[2] = PPM_CHANNEL_MIN_US;          // Throttle MIN
  ppm_us[3] = PPM_CHANNEL_MID_US;          // Yaw   centro
  ppm_us[4] = PPM_CHANNEL_MIN_US;          // Arm OFF
  ppm_us[5] = PPM_CHANNEL_MID_US;
  ppm_us[6] = PPM_CHANNEL_MID_US;
  ppm_us[7] = PPM_CHANNEL_MID_US;
}

// =============================================================================
// RMT PPM — clock 1 MHz (1 tick = 1 us), idle LOW
// =============================================================================
#define PPM_RMT_CHAN  RMT_CHANNEL_0

static void setupPpmRmt() {
  rmt_config_t rmt_cfg = {};
  rmt_cfg.rmt_mode      = RMT_MODE_TX;
  rmt_cfg.channel       = PPM_RMT_CHAN;
  rmt_cfg.gpio_num      = (gpio_num_t)PIN_PPM_TX;
  rmt_cfg.clk_div       = 80;   // 80 MHz / 80 = 1 MHz → 1 tick = 1 us
  rmt_cfg.mem_block_num = 1;
  rmt_cfg.tx_config.loop_en        = false;
  rmt_cfg.tx_config.carrier_en     = false;
  rmt_cfg.tx_config.idle_output_en = true;
  rmt_cfg.tx_config.idle_level     = RMT_IDLE_LEVEL_LOW;  // PPM positivo: idle LOW
  rmt_config(&rmt_cfg);
  rmt_driver_install(PPM_RMT_CHAN, 0, 0);
  gpio_set_drive_capability((gpio_num_t)PIN_PPM_TX, GPIO_DRIVE_CAP_3);
}

static void ppmTransmitRmt() {
  // Item por canal: HIGH 300us + LOW (ch_us - 300us)
  // + 1 item de sync (LOW longo) no final
  rmt_item32_t items[PPM_NUM_CHANNELS + 1];

  for (int i = 0; i < PPM_NUM_CHANNELS; i++) {
    uint16_t ch_us = ppm_us[i];
    if (ch_us < 1000) ch_us = 1000;
    if (ch_us > 2000) ch_us = 2000;
    items[i].level0    = 1;                            // HIGH pulse
    items[i].duration0 = PPM_PULSE_HIGH_US;            // 300 us
    items[i].level1    = 0;                            // LOW gap
    items[i].duration1 = ch_us - PPM_PULSE_HIGH_US;    // 700..1700 us
  }
  // Sync gap final
  items[PPM_NUM_CHANNELS].level0    = 0;
  items[PPM_NUM_CHANNELS].duration0 = PPM_SYNC_GAP_US / 2;
  items[PPM_NUM_CHANNELS].level1    = 0;
  items[PPM_NUM_CHANNELS].duration1 = PPM_SYNC_GAP_US / 2;

  rmt_write_items(PPM_RMT_CHAN, items, PPM_NUM_CHANNELS + 1, true);
}

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0 < 2000)) { delay(10); }

  Serial.println();
  Serial.println(F("==========================================="));
  Serial.println(F("  ESP32-C3 RX_PPM — nRF24 V2 → PPM 8ch"));
  Serial.println(F("==========================================="));

  setupPpmRmt();
  Serial.printf("PPM: RMT GPIO%d, 8ch, positivo, ~50Hz\n", PIN_PPM_TX);

  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_NRF_CSN);

  if (!radio.begin()) {
    Serial.println(F("ERRO: nRF24 não inicializou."));
  } else {
    radio.setPALevel(RF24_PA_LOW);
    radio.setDataRate(RF24_250KBPS);
    radio.setChannel(RF_CHANNEL);
    radio.setAutoAck(false);
    radio.setPayloadSize(sizeof(PacketRF));
    radio.openReadingPipe(0, RF_ADDRESS);
    radio.startListening();
    Serial.printf("Radio OK. addr=%s ch=%u\n", RF_ADDRESS, RF_CHANNEL);
  }

  setFailsafePpm();
  lastRxMs = millis();
  lastPpmUs = micros();
  Serial.println(F("Aguardando pacotes V2..."));
}

// =============================================================================
// LOOP
// =============================================================================
void loop() {
  bool got = false;
  while (radio.available()) {
    radio.read(&pkt, sizeof(pkt));
    got = true;
  }

  if (got) {
    mapPacketToPpm(pkt);
    lastRxMs = millis();
    rxCount++;
  }

  bool failsafe = (millis() - lastRxMs) > RX_TIMEOUT_MS;
  if (failsafe) setFailsafePpm();

  // Manda frame PPM a cada ~20 ms (50 Hz) — gerenciado pelo próprio RMT que bloqueia até terminar
  uint32_t now = micros();
  if ((now - lastPpmUs) >= 20000) {
    lastPpmUs = now;
    ppmTransmitRmt();
    if (failsafe) failsafeFrames++;
  }

  // Debug a 2 Hz
  static uint32_t lastDbg = 0;
  if (millis() - lastDbg >= 500) {
    lastDbg = millis();
    Serial.printf("rx=%lu fs=%lu ch1..4=[%4u %4u %4u %4u] arm=%u mode=%u %s\n",
                  (unsigned long)rxCount, (unsigned long)failsafeFrames,
                  ppm_us[0], ppm_us[1], ppm_us[2], ppm_us[3],
                  ppm_us[4] > PPM_CHANNEL_MID_US ? 1 : 0,
                  ppm_us[5] > PPM_CHANNEL_MID_US ? 1 : 0,
                  failsafe ? "[FAILSAFE]" : "");
  }
}
