// =============================================================================
// C3_RX_SBUS — Receptor nRF24 → SBUS para Betaflight (Matek F411-RX)
// Plataforma: ESP32-C3 Super Mini (RISC-V, single-core, USB-C nativo)
// Parte do OpenRC-AeroLink
// =============================================================================
//
// FUNÇÃO
//   Recebe PacketRF do NANO_TX via nRF24L01 e gera stream SBUS de 25 bytes
//   a ~71 Hz pelo UART1 invertido (GPIO21) → pad RX2 da Matek F411-RX.
//
// CONTRATO RÁDIO (deve casar com NANO_TX):
//   nRF24: address "00001", channel 76, 250 kbps, AutoAck OFF, payload 10 bytes
//   struct PacketRF { uint8_t p[8]; uint8_t s1; uint8_t s2; }
//   p[0]=THR, p[1]=RUD, p[2]=AIL, p[3]=ELE, p[4..7]=aux
//
// MAPEAMENTO → SBUS (padrão AETR Betaflight):
//   CH1 = Roll  (AIL = p[2])
//   CH2 = Pitch (ELE = p[3])
//   CH3 = Throttle   (p[0])
//   CH4 = Yaw   (RUD = p[1])
//   CH5 = AUX1  (s1 = arm)
//   CH6 = AUX2  (s2 = mode)
//   CH7..CH10 = aux pots (p[4..7])
//   CH11..CH16 = neutro (992)
//
// SBUS (camada física):
//   100 000 bps, 8 data + even parity + 2 stop bits, LÓGICA INVERTIDA
//   Frame 25 bytes: 0x0F | 16ch×11bit packed LE | flags | 0x00
//   Período: 14 ms (~71 Hz)
//   Range: 172 (988µs) .. 992 (1500µs) .. 1811 (2012µs)
//
// FAILSAFE
//   >200 ms sem pacote → flag failsafe + frame_lost no SBUS,
//   throttle MIN, arm OFF, demais centrados.
//
// =============================================================================
// PINAGEM (ESP32-C3 Super Mini)
// =============================================================================
//
//   ┌──── nRF24L01 PA+LNA ────┐         ┌──── Matek F411-RX ────┐
//   │  GND   ── GND           │         │  GND   ── GND          │
//   │  VCC   ── 3V3           │         │  RX2   ── GPIO21 ◄─────┤ SBUS TX
//   │  CE    ── GPIO10        │         │  (opc) 5V → 5V do ESP  │
//   │  CSN   ── GPIO7         │         └────────────────────────┘
//   │  SCK   ── GPIO4         │
//   │  MOSI  ── GPIO6         │     Debug: USB-C nativo (Serial)
//   │  MISO  ── GPIO5         │
//   │  IRQ   ── (NC)          │
//   └─────────────────────────┘
//
// ⚠️ HARDWARE:
//   - Cap eletrolítico 10–100 µF entre VCC e GND do nRF24 (evita resets).
//   - GND do ESP32-C3 ↔ GND da Matek OBRIGATÓRIO.
//   - nRF24 é 3.3V — usar pino 3V3 do C3.
// =============================================================================

#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include "driver/uart.h"

// =============================================================================
// PINAGEM
// =============================================================================
static constexpr int PIN_NRF_CE   = 10;
static constexpr int PIN_NRF_CSN  = 7;
static constexpr int PIN_SPI_SCK  = 4;
static constexpr int PIN_SPI_MISO = 5;
static constexpr int PIN_SPI_MOSI = 6;

static constexpr int PIN_SBUS_TX  = 21;   // UART1 TX (invertido p/ SBUS)

// =============================================================================
// CONFIG RÁDIO — DEVE casar com NANO_TX
// =============================================================================
static const byte    RF_ADDRESS[6] = "00001";
static constexpr uint8_t RF_CHANNEL = 76;

// =============================================================================
// CONFIG SBUS
// =============================================================================
static constexpr uint32_t SBUS_BAUD        = 100000;
static constexpr uint32_t SBUS_INTERVAL_US = 14000;   // 14 ms ≈ 71 Hz
static constexpr uint32_t RX_TIMEOUT_MS    = 200;     // failsafe se sem pacote

// Inversão SBUS por hardware do ESP32-C3.
// true  = ESP inverte (idle LOW, padrão SBUS). No BF: serialrx_inverted = OFF.
// false = ESP NÃO inverte (idle HIGH).          No BF: serialrx_inverted = ON.
static constexpr bool SBUS_INVERT_HW = true;

// Faixa de canal SBUS (convenção FrSky)
static constexpr uint16_t SBUS_MIN = 172;    // 988 µs
static constexpr uint16_t SBUS_MID = 992;    // 1500 µs
static constexpr uint16_t SBUS_MAX = 1811;   // 2012 µs

// =============================================================================
// ESTRUTURA DO PACOTE (idêntica ao NANO_TX)
// =============================================================================
struct __attribute__((packed)) PacketRF {
  uint8_t p[8];   // canais analógicos 0..255
  uint8_t s1;     // switch 1 (arm)
  uint8_t s2;     // switch 2 (mode)
};
static PacketRF pkt;

// =============================================================================
// ESTADO
// =============================================================================
static RF24 radio(PIN_NRF_CE, PIN_NRF_CSN);

static uint16_t sbus_ch[16] = {0};
static uint32_t lastRxMs       = 0;
static uint32_t lastSbusUs     = 0;
static uint32_t rxCount        = 0;
static uint32_t failsafeFrames = 0;

// =============================================================================
// CONVERSÃO
// =============================================================================

// 0..255 → 172..1811 (SBUS). Centro: 128 → ~992.
static inline uint16_t byteToSbus(uint8_t b) {
  return SBUS_MIN + ((uint32_t)b * (SBUS_MAX - SBUS_MIN) + 127) / 255;
}

// Bool → SBUS_MIN ou SBUS_MAX.
static inline uint16_t boolToSbus(uint8_t v) {
  return v ? SBUS_MAX : SBUS_MIN;
}

// =============================================================================
// MAPEAMENTO PacketRF → 16 canais SBUS (AETR)
// =============================================================================
static void mapPacketToSbus(const PacketRF& p) {
  sbus_ch[0]  = byteToSbus(p.p[2]);   // CH1  Roll   ← AIL
  sbus_ch[1]  = byteToSbus(p.p[3]);   // CH2  Pitch  ← ELE
  sbus_ch[2]  = byteToSbus(p.p[0]);   // CH3  Throttle
  sbus_ch[3]  = byteToSbus(p.p[1]);   // CH4  Yaw    ← RUD
  sbus_ch[4]  = boolToSbus(p.s1);     // CH5  AUX1   ← arm
  sbus_ch[5]  = boolToSbus(p.s2);     // CH6  AUX2   ← mode
  sbus_ch[6]  = byteToSbus(p.p[4]);   // CH7  AUX3
  sbus_ch[7]  = byteToSbus(p.p[5]);   // CH8  AUX4
  sbus_ch[8]  = byteToSbus(p.p[6]);   // CH9  AUX5
  sbus_ch[9]  = byteToSbus(p.p[7]);   // CH10 AUX6
  for (int i = 10; i < 16; i++) sbus_ch[i] = SBUS_MID;
}

static void setFailsafeChannels() {
  sbus_ch[0] = SBUS_MID;   // Roll  centro
  sbus_ch[1] = SBUS_MID;   // Pitch centro
  sbus_ch[2] = SBUS_MIN;   // Throttle MIN
  sbus_ch[3] = SBUS_MID;   // Yaw   centro
  sbus_ch[4] = SBUS_MIN;   // Arm   OFF
  for (int i = 5; i < 16; i++) sbus_ch[i] = SBUS_MID;
}

// =============================================================================
// ENCODER SBUS — 16 canais × 11 bits → 22 bytes (LSB first)
// =============================================================================
static void buildSbusFrame(uint8_t frame[25], bool failsafe, bool frameLost) {
  frame[0] = 0x0F;
  for (int i = 1; i <= 22; i++) frame[i] = 0;

  for (int ch = 0; ch < 16; ch++) {
    uint32_t v = sbus_ch[ch] & 0x07FF;
    uint32_t bitPos  = (uint32_t)ch * 11;
    uint32_t byteIdx = (bitPos / 8) + 1;
    uint8_t  bitShift = bitPos % 8;
    frame[byteIdx    ] |= (uint8_t)((v << bitShift) & 0xFF);
    frame[byteIdx + 1] |= (uint8_t)((v >> (8 - bitShift)) & 0xFF);
    if (bitShift > 5) {
      frame[byteIdx + 2] |= (uint8_t)((v >> (16 - bitShift)) & 0xFF);
    }
  }

  uint8_t flags = 0;
  if (frameLost) flags |= (1 << 2);
  if (failsafe)  flags |= (1 << 3);
  frame[23] = flags;
  frame[24] = 0x00;
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
  Serial.println(F("  C3_RX_SBUS — nRF24 → SBUS (Betaflight)"));
  Serial.println(F("==========================================="));

  // --- UART1 via IDF (bypass bugs Arduino no C3) ---
  uart_config_t uart_cfg = {};
  uart_cfg.baud_rate  = (int)SBUS_BAUD;
  uart_cfg.data_bits  = UART_DATA_8_BITS;
  uart_cfg.parity     = UART_PARITY_EVEN;
  uart_cfg.stop_bits  = UART_STOP_BITS_2;
  uart_cfg.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
  uart_cfg.source_clk = UART_SCLK_APB;

  uart_param_config(UART_NUM_1, &uart_cfg);
  uart_set_pin(UART_NUM_1, PIN_SBUS_TX,
               UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

  if (SBUS_INVERT_HW) {
    uart_set_line_inverse(UART_NUM_1, UART_SIGNAL_TXD_INV);
  } else {
    uart_set_line_inverse(UART_NUM_1, UART_SIGNAL_INV_DISABLE);
  }
  uart_driver_install(UART_NUM_1, 256, 256, 0, NULL, 0);

  Serial.printf("SBUS: UART1 TX=GPIO%d, 100kbps 8E2 %s\n",
                PIN_SBUS_TX,
                SBUS_INVERT_HW ? "INVERTIDO (BF: serialrx_inverted=OFF)"
                               : "NORMAL (BF: serialrx_inverted=ON)");

  // --- SPI (GPIO matrix do C3 permite roteamento livre) ---
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_NRF_CSN);
  Serial.printf("SPI: SCK=%d MISO=%d MOSI=%d CSN=%d CE=%d\n",
                PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_NRF_CSN, PIN_NRF_CE);

  // --- nRF24 ---
  if (!radio.begin()) {
    Serial.println(F("ERRO: nRF24 não inicializou. Cheque ligações/cap 10uF."));
  } else {
    radio.setPALevel(RF24_PA_LOW);       // subir pra HIGH/MAX em campo
    radio.setDataRate(RF24_250KBPS);
    radio.setChannel(RF_CHANNEL);
    radio.setAutoAck(false);
    radio.setPayloadSize(sizeof(PacketRF));
    radio.openReadingPipe(0, RF_ADDRESS);
    radio.startListening();
    Serial.printf("Radio OK — addr=%s ch=%u 250kbps payload=%u bytes\n",
                  RF_ADDRESS, RF_CHANNEL, (unsigned)sizeof(PacketRF));
  }

  setFailsafeChannels();
  lastRxMs   = millis();
  lastSbusUs = micros();

  Serial.println(F("Aguardando pacotes do NANO_TX..."));
}

// =============================================================================
// LOOP
// =============================================================================
void loop() {
  // 1) Drena FIFO do nRF24 — usa pacote mais recente
  bool got = false;
  while (radio.available()) {
    radio.read(&pkt, sizeof(pkt));
    got = true;
  }

  if (got) {
    mapPacketToSbus(pkt);
    lastRxMs = millis();
    rxCount++;
  }

  // 2) Failsafe
  bool failsafe = (millis() - lastRxMs) > RX_TIMEOUT_MS;
  if (failsafe) setFailsafeChannels();

  // 3) Envia frame SBUS a cada 14 ms
  uint32_t now = micros();
  static uint8_t lastFrame[25] = {0};
  if ((now - lastSbusUs) >= SBUS_INTERVAL_US) {
    lastSbusUs = now;
    uint8_t frame[25];
    buildSbusFrame(frame, failsafe, failsafe);
    uart_write_bytes(UART_NUM_1, (const char*)frame, sizeof(frame));
    memcpy(lastFrame, frame, 25);
    if (failsafe) failsafeFrames++;
  }

  // 4) Debug USB-CDC a 2 Hz
  static uint32_t lastDbg = 0;
  if (millis() - lastDbg >= 500) {
    lastDbg = millis();
    Serial.printf("rx=%lu fs=%lu ch=[%4u %4u %4u %4u] arm=%u mode=%u %s\n",
                  (unsigned long)rxCount, (unsigned long)failsafeFrames,
                  sbus_ch[0], sbus_ch[1], sbus_ch[2], sbus_ch[3],
                  sbus_ch[4] > SBUS_MID ? 1 : 0,
                  sbus_ch[5] > SBUS_MID ? 1 : 0,
                  failsafe ? "[FAILSAFE]" : "");

    Serial.print("SBUS hex: ");
    for (int i = 0; i < 25; i++) {
      if (lastFrame[i] < 0x10) Serial.print('0');
      Serial.print(lastFrame[i], HEX);
      Serial.print(' ');
    }
    Serial.println();
  }
}
