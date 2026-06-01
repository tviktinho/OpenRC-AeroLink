// =============================================================================
// ESP32_RX_SBUS — Receptor caseiro nRF24 V2 → SBUS para FC comercial
// Plataforma: ESP32 DevKit (WROOM-32). Arduino IDE.
// Parte do OpenRC-AeroLink — TCC.
// =============================================================================
// FUNÇÃO
//   Escuta o protocolo nRF24 V2 do NANO_TX_V2 e converte o payload em um stream
//   SBUS de 25 bytes a ~71 Hz, enviado pelo UART2 do ESP32 (GPIO17) pro pad R2
//   de uma Flight Controller Betaflight (testado contra Matek F411-RX, BF 4.1.6).
//
// CONTRATO V2 (compatível com NANO_RX_v2.0.ino e 14-BRIZA.ino):
//   address = "00001", channel 76, 250 kbps, AutoAck OFF
//   struct PacketRF { uint8_t p[8]; uint8_t s1; uint8_t s2; } // 10 bytes
//   p[0]=THR, p[1]=RUD, p[2]=AIL, p[3]=ELE, p[4..7]=aux
//
// MAPEAMENTO V2 → SBUS (padrão AETR do Betaflight):
//   CH1=Roll(AIL), CH2=Pitch(ELE), CH3=Throttle, CH4=Yaw(RUD)
//   CH5=s1 (arm), CH6=s2 (mode), CH7..10=aux pots p[4..7], CH11..16=0
//
// SBUS (camada física)
//   100 000 bps, 8 data + even parity + 2 stop bits, lógica INVERTIDA
//   Frame de 25 bytes: 0x0F | 16ch×11bit packed LE | flags | 0x00
//   Janela: 7–14 ms. Aqui: 14 ms (~71 Hz).
//   Faixa de cada canal: 0..2047. Convenção FrSky: 172=988µs, 992=1500µs,
//   1811=2012µs. Fórmula: sbus = (us - 1500) * 8 / 5 + 992.
//
// FAILSAFE
//   Se ficar > RX_TIMEOUT_MS sem pacote do TX, envia frame SBUS com flag
//   failsafe (bit 3 do byte 23) E frame_lost (bit 2), throttle no mínimo
//   (172) e os outros canais em centro (992). A FC vai entrar em modo
//   failsafe baseado nisso (cortar motores ou ir pro RTH se configurado).
//
// PINAGEM (ESP32 DevKit)
//   nRF24L01 (PA+LNA): VCC=3V3, GND=GND, CE=GPIO4, CSN=GPIO5,
//                      SCK=GPIO18, MOSI=GPIO23, MISO=GPIO19, IRQ=NC
//   SBUS para FC:      TX2=GPIO17 → pad R2 da Matek; GND comum.
//
// ⚠️ HARDWARE
//   - Capacitor 10–100 µF entre VCC e GND do nRF24 PA+LNA (resets sob TX).
//   - GND ESP32 ↔ GND Matek OBRIGATÓRIO mesmo com ESP alimentado por USB.
//   - nRF24 é 3.3V — NÃO ligar em 5V.
// =============================================================================

#include <SPI.h>
#include <RF24.h>

// =============================================================================
// PINAGEM
// =============================================================================
#define PIN_NRF_CE        4
#define PIN_NRF_CSN       5
#define PIN_SBUS_TX      17     // GPIO17 = TX2 default do ESP32

// =============================================================================
// CONFIG RÁDIO — DEVE casar com o NANO_TX_V2
// =============================================================================
const byte    RF_ADDRESS[6]   = "00001";
const uint8_t RF_CHANNEL      = 76;
// PA_LOW como default seguro pra bench. Subir pra HIGH/MAX quando validar no campo.

// =============================================================================
// CONFIG SBUS
// =============================================================================
const uint32_t SBUS_BAUD          = 100000;
const uint32_t SBUS_INTERVAL_US   = 14000;   // ~71 Hz; FrSky XSR usa 9000
const uint32_t RX_TIMEOUT_MS      = 200;     // failsafe se sem pacote por > 200ms

// SBUS channel range (convenção FrSky)
const uint16_t SBUS_MIN  = 172;    // = 988 µs
const uint16_t SBUS_MID  = 992;    // = 1500 µs
const uint16_t SBUS_MAX  = 1811;   // = 2012 µs

// =============================================================================
// ESTRUTURA RX (idêntica ao TX_V2 e ao NANO_RX_v2.0)
// =============================================================================
struct __attribute__((packed)) PacketRF {
  uint8_t p[8];
  uint8_t s1;
  uint8_t s2;
} pkt;

// =============================================================================
// ESTADO
// =============================================================================
RF24 radio(PIN_NRF_CE, PIN_NRF_CSN);

uint16_t sbus_ch[16] = {0};   // 0..2047 cada
uint32_t lastRxMs = 0;
uint32_t lastSbusUs = 0;
uint32_t rxCount = 0;
uint32_t failsafeFrames = 0;

// =============================================================================
// UTILS
// =============================================================================

// 0..255 (byte do TX V2) → 172..1811 (SBUS). Center 128→992.
inline uint16_t byteToSbus(uint8_t b) {
  // Mapeia [0..255] linearmente em [SBUS_MIN..SBUS_MAX].
  return SBUS_MIN + ((uint32_t)b * (SBUS_MAX - SBUS_MIN) + 127) / 255;
}

// Switch binário (0/1) → SBUS_MIN ou SBUS_MAX. Útil pra arm/mode.
inline uint16_t boolToSbus(uint8_t v) {
  return v ? SBUS_MAX : SBUS_MIN;
}

// =============================================================================
// MAPEAMENTO V2 → 16 canais SBUS
// =============================================================================
void mapPacketToSbus(const PacketRF& p) {
  // Padrão AETR (Betaflight default)
  sbus_ch[0]  = byteToSbus(p.p[2]);   // CH1 Roll  ← AIL
  sbus_ch[1]  = byteToSbus(p.p[3]);   // CH2 Pitch ← ELE
  sbus_ch[2]  = byteToSbus(p.p[0]);   // CH3 Throttle
  sbus_ch[3]  = byteToSbus(p.p[1]);   // CH4 Yaw   ← RUD
  sbus_ch[4]  = boolToSbus(p.s1);     // CH5 AUX1  ← s1 (arm)
  sbus_ch[5]  = boolToSbus(p.s2);     // CH6 AUX2  ← s2 (mode)
  sbus_ch[6]  = byteToSbus(p.p[4]);   // CH7 AUX3  ← aux pot
  sbus_ch[7]  = byteToSbus(p.p[5]);   // CH8 AUX4
  sbus_ch[8]  = byteToSbus(p.p[6]);   // CH9 AUX5
  sbus_ch[9]  = byteToSbus(p.p[7]);   // CH10 AUX6
  for (int i = 10; i < 16; i++) sbus_ch[i] = SBUS_MID;
}

void setFailsafeChannels() {
  // Throttle mínimo, demais centrados — FC vai cortar motor com flag failsafe.
  sbus_ch[0]  = SBUS_MID;             // Roll  centro
  sbus_ch[1]  = SBUS_MID;             // Pitch centro
  sbus_ch[2]  = SBUS_MIN;             // Throttle MIN
  sbus_ch[3]  = SBUS_MID;             // Yaw   centro
  sbus_ch[4]  = SBUS_MIN;             // Arm OFF
  for (int i = 5; i < 16; i++) sbus_ch[i] = SBUS_MID;
}

// =============================================================================
// ENCODER SBUS — empacota 16 canais × 11 bits em 22 bytes (LSB first)
// =============================================================================
void buildSbusFrame(uint8_t frame[25], bool failsafe, bool frameLost) {
  frame[0] = 0x0F;  // start byte

  // Zera o miolo
  for (int i = 1; i <= 22; i++) frame[i] = 0;

  // Cada canal ocupa 11 bits consecutivos no stream de bits começando no
  // byte 1. Posição em bits do canal i = i * 11. byte index = (bit_pos / 8) + 1.
  for (int ch = 0; ch < 16; ch++) {
    uint32_t v = sbus_ch[ch] & 0x07FF;       // 11 bits
    uint32_t bitPos = (uint32_t)ch * 11;
    uint32_t byteIdx = (bitPos / 8) + 1;     // +1 por causa do start byte
    uint8_t  bitShift = bitPos % 8;
    // O valor pode atravessar até 3 bytes (11 bits + offset 7 = 18 bits).
    frame[byteIdx    ] |= (uint8_t)((v << bitShift) & 0xFF);
    frame[byteIdx + 1] |= (uint8_t)((v >> (8 - bitShift)) & 0xFF);
    if (bitShift > 5) {  // espalha pro 3º byte só se necessário
      frame[byteIdx + 2] |= (uint8_t)((v >> (16 - bitShift)) & 0xFF);
    }
  }

  // Byte de flags
  uint8_t flags = 0;
  if (frameLost) flags |= (1 << 2);
  if (failsafe)  flags |= (1 << 3);
  frame[23] = flags;

  frame[24] = 0x00;  // end byte
}

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println(F("==========================================="));
  Serial.println(F("  ESP32_RX_SBUS — nRF24 V2 → SBUS bridge"));
  Serial.println(F("==========================================="));

  // UART2 invertido pro SBUS (idle LOW), 100 kbps 8E2, sem RX usado.
  // O 6º parâmetro `invert=true` ativa o inversor de hardware do ESP32.
  Serial2.begin(SBUS_BAUD, SERIAL_8E2, /*rxPin*/ -1, /*txPin*/ PIN_SBUS_TX, /*invert*/ true);
  Serial.printf("SBUS: UART2 TX=%d, 100kbps 8E2 invertido\n", PIN_SBUS_TX);

  // Rádio
  SPI.begin();   // VSPI default: SCK=18, MISO=19, MOSI=23
  if (!radio.begin()) {
    Serial.println(F("ERRO: nRF24 não inicializou. Cheque ligações/cap 10uF."));
  } else {
    radio.setPALevel(RF24_PA_LOW);
    radio.setDataRate(RF24_250KBPS);
    radio.setChannel(RF_CHANNEL);
    radio.setAutoAck(false);
    radio.setPayloadSize(sizeof(PacketRF));
    radio.openReadingPipe(0, RF_ADDRESS);
    radio.startListening();
    Serial.printf("Radio OK. addr=%s ch=%u 250kbps, payload=%u bytes\n",
                  RF_ADDRESS, RF_CHANNEL, (unsigned)sizeof(PacketRF));
  }

  // Inicia em failsafe; só sai quando chegar o 1º pacote
  setFailsafeChannels();
  lastRxMs = millis();
  lastSbusUs = micros();

  Serial.println(F("Aguardando pacotes V2..."));
}

// =============================================================================
// LOOP
// =============================================================================
void loop() {
  // 1) Drena tudo que tiver no FIFO do nRF24 (usa o ÚLTIMO pacote — mais fresco)
  bool got = false;
  while (radio.available()) {
    radio.read(&pkt, sizeof(pkt));
    got = true;
  }

  // 2) Atualiza mapeamento se chegou pacote novo
  if (got) {
    mapPacketToSbus(pkt);
    lastRxMs = millis();
    rxCount++;
  }

  // 3) Decide failsafe
  bool failsafe = (millis() - lastRxMs) > RX_TIMEOUT_MS;
  if (failsafe) setFailsafeChannels();

  // 4) Envia frame SBUS no intervalo configurado
  uint32_t now = micros();
  if ((now - lastSbusUs) >= SBUS_INTERVAL_US) {
    lastSbusUs = now;
    uint8_t frame[25];
    buildSbusFrame(frame, /*failsafe*/ failsafe, /*frameLost*/ failsafe);
    Serial2.write(frame, sizeof(frame));
    if (failsafe) failsafeFrames++;
  }

  // 5) Telemetria de debug (USB) a 2 Hz
  static uint32_t lastDbg = 0;
  if (millis() - lastDbg >= 500) {
    lastDbg = millis();
    Serial.printf("rx=%lu fs_frames=%lu ch1..4=[%4u %4u %4u %4u] arm=%u mode=%u %s\n",
                  rxCount, failsafeFrames,
                  sbus_ch[0], sbus_ch[1], sbus_ch[2], sbus_ch[3],
                  sbus_ch[4] > SBUS_MID ? 1 : 0,
                  sbus_ch[5] > SBUS_MID ? 1 : 0,
                  failsafe ? "[FAILSAFE]" : "");
  }
}
