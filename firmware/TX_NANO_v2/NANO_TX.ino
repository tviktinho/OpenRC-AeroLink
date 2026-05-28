/* =============================================================================
 * NANO_TX v2 — Transmissor OpenRC AeroLink (upgrade ELRS, Fase 1)
 * Plataforma: Arduino Nano (ATmega328P @ 16 MHz)
 *
 * ESTRATÉGIA DE DUAL-OUTPUT:
 *   • nRF24L01 (SPI) -> continua emitindo a struct de 10 bytes para os RX
 *     antigos (NANO_RX, ESP32 FC, MEGA_RX, 14-BRIZA, etc.).  Zero downtime.
 *   • UART HW (TX0/D1) -> emite frames CRSF @420 000 baud para o módulo
 *     Heltec V2 que roda ExpressLRS oficial e gera o link 900 MHz.
 *
 * MAPEAMENTO DE CANAIS CRSF (8 úteis, restante neutro):
 *   CH1 = pot A0  (mapeado 0..255 -> CRSF 172..1811)
 *   CH2 = pot A1
 *   CH3 = pot A2
 *   CH4 = pot A3
 *   CH5 = pot A4
 *   CH6 = pot A5
 *   CH7 = pot A6
 *   CH8 = bitmask switches:  SW1 | SW2<<1 | SW3<<2 | SW4<<3   (0..15, 16 níveis)
 *   CH9..CH16 = 992 (centro, reservado p/ Fase 2)
 *
 * MAPEAMENTO nRF24 (compatibilidade total com legacy):
 *   struct PacketRF { uint8_t p[8]; uint8_t s1; uint8_t s2; } — mesma do v1.
 *   Endereço "00001", canal 76, 250 kbps, AutoAck OFF.
 *
 * ATENÇÃO AO FLASH:
 *   A HW Serial (TX0/D1, RX0/D0) é a MESMA usada pelo bootloader USB. Durante
 *   a programação, o conversor USB-Serial fica brigando com o link do Heltec.
 *   Soluções:
 *     • Jumper físico no D1 que desconecta o Heltec durante o upload, OU
 *     • Desligar o Heltec antes de flashar o Nano, OU
 *     • Usar um programador ISP externo (skipa o bootloader USB)
 *
 * Pinout (igual ao v1 legacy):
 *   nRF24L01:   CE=D8, CSN=D7, MOSI=D11, MISO=D12, SCK=D13
 *   Pots:       A0..A6 (7 pots, 7º livre se usar shield diferente)
 *   Switches:   D2 (SW1), D3 (SW2), D4 (SW3), D5 (SW4) — INPUT_PULLUP, ativo LOW
 *   UART CRSF:  TX0/D1 -> [divisor 3k3/1k8] -> GPIO 17 do Heltec V2
 *               (ESP32 NÃO é 5V-tolerant — divisor de tensão é obrigatório.
 *                Alternativa: level shifter bidirecional tipo TXS0108/BSS138.)
 *
 * ============================================================================= */

#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include "crsf_tx.h"

// =============================================================================
// PINAGEM
// =============================================================================
#define PIN_NRF_CE      8
#define PIN_NRF_CSN     7

#define PIN_POT_0       A0
#define PIN_POT_1       A1
#define PIN_POT_2       A2
#define PIN_POT_3       A3
#define PIN_POT_4       A4
#define PIN_POT_5       A5
#define PIN_POT_6       A6

#define PIN_SW1         2
#define PIN_SW2         3
#define PIN_SW3         4
#define PIN_SW4         5

#define PIN_LED         LED_BUILTIN

// =============================================================================
// CONFIGURAÇÃO RF (nRF24, legado)
// =============================================================================
#define RF_CHANNEL          76
#define RF_PAYLOAD_BYTES    10
static const byte RF_ADDRESS[6] = "00001";

// =============================================================================
// TEMPO / TAXAS
// =============================================================================
#define LOOP_INTERVAL_MS     20  // 50 Hz combina com taxa típica de ELRS 900 (50/100/200 Hz)
                                 // e dá folga ao nRF24 (que aceita 50 Hz tranquilo).

// =============================================================================
// ESTRUTURA DO PACOTE nRF24 (idêntica ao legacy)
// =============================================================================
struct __attribute__((packed)) PacketRF {
    uint8_t p[8];   // 8 canais 0..255 (mantemos 8 para compat; ch7 = 128 neutro)
    uint8_t s1;     // switch 1
    uint8_t s2;     // switch 2
} pkt;

// =============================================================================
// OBJETOS GLOBAIS
// =============================================================================
RF24 radio(PIN_NRF_CE, PIN_NRF_CSN);

// Buffer de canais CRSF (16 canais, escala 172..1811)
uint16_t crsf_channels[CRSF_NUM_CHANNELS];

// Frame CRSF pronto para serializar
uint8_t  crsf_frame[CRSF_FRAME_SIZE];

// Estatísticas / debug
uint32_t lastLoopMs   = 0;
uint32_t txCountNrf   = 0;
uint32_t txCountCrsf  = 0;
bool     nrfReady     = false;

// =============================================================================
// INICIALIZAÇÃO DO nRF24
// =============================================================================
static bool initRadio() {
    delay(20);
    if (!radio.begin()) return false;

    pinMode(10, OUTPUT);                       // boa prática SPI master no Nano
    radio.setPALevel(RF24_PA_LOW);
    radio.setDataRate(RF24_250KBPS);
    radio.setChannel(RF_CHANNEL);
    radio.setAutoAck(false);
    radio.setRetries(0, 0);
    radio.setPayloadSize(sizeof(PacketRF));
    radio.openWritingPipe(RF_ADDRESS);
    radio.stopListening();
    return radio.isChipConnected();
}

// =============================================================================
// LEITURA DAS ENTRADAS
// =============================================================================
static inline uint8_t readPotByte(uint8_t pin) {
    // 10-bit -> 8-bit (>>2). Sem média móvel aqui (taxa de 50 Hz já é suave o
    // suficiente; ELRS faz seu próprio LPF do outro lado).
    return (uint8_t)(analogRead(pin) >> 2);
}

static void readInputs() {
    pkt.p[0] = readPotByte(PIN_POT_0);
    pkt.p[1] = readPotByte(PIN_POT_1);
    pkt.p[2] = readPotByte(PIN_POT_2);
    pkt.p[3] = readPotByte(PIN_POT_3);
    pkt.p[4] = readPotByte(PIN_POT_4);
    pkt.p[5] = readPotByte(PIN_POT_5);
    pkt.p[6] = readPotByte(PIN_POT_6);
    pkt.p[7] = 128;  // reservado / centro

    // Switches: INPUT_PULLUP, ativo LOW -> 1 quando pressionado
    // pkt.s1 e pkt.s2 são da struct nRF24 legado (compat).
    pkt.s1 = (digitalRead(PIN_SW1) == LOW) ? 1 : 0;
    pkt.s2 = (digitalRead(PIN_SW2) == LOW) ? 1 : 0;
}

// =============================================================================
// MONTAGEM DOS 16 CANAIS CRSF (a partir dos inputs já lidos)
// =============================================================================
static void buildCrsfChannels() {
    // CH1..CH7 = pots A0..A6
    for (uint8_t i = 0; i < 7; i++) {
        crsf_channels[i] = crsf_byte_to_channel(pkt.p[i]);
    }

    // CH8 = bitmask dos 4 switches concatenados (0..15 -> 16 níveis)
    // Lemos SW3/SW4 aqui (não estão na struct nRF24 legado, só CH8 do CRSF).
    uint8_t sw_mask = 0;
    if (pkt.s1)                              sw_mask |= (1 << 0);
    if (pkt.s2)                              sw_mask |= (1 << 1);
    if (digitalRead(PIN_SW3) == LOW)         sw_mask |= (1 << 2);
    if (digitalRead(PIN_SW4) == LOW)         sw_mask |= (1 << 3);
    // Escala 0..15 -> 172..1811 (passos de ~109)
    crsf_channels[7] = (uint16_t)(CRSF_CHANNEL_VALUE_MIN
                                  + ((uint32_t)sw_mask * (CRSF_CHANNEL_VALUE_MAX - CRSF_CHANNEL_VALUE_MIN) + 7) / 15);

    // CH9..CH16 = centro (992), reservado para uso futuro (Fase 2)
    for (uint8_t i = 8; i < CRSF_NUM_CHANNELS; i++) {
        crsf_channels[i] = CRSF_CHANNEL_VALUE_MID;
    }
}

// =============================================================================
// SETUP
// =============================================================================
void setup() {
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, HIGH);  // LED ligado durante init

    // Switches
    pinMode(PIN_SW1, INPUT_PULLUP);
    pinMode(PIN_SW2, INPUT_PULLUP);
    pinMode(PIN_SW3, INPUT_PULLUP);
    pinMode(PIN_SW4, INPUT_PULLUP);

    // HW Serial @ 420 000 baud — SEM prints de texto: linha é dedicada ao CRSF
    Serial.begin(CRSF_BAUDRATE);

    // nRF24 (legado). Se falhar, NÃO travamos — CRSF continua funcionando.
    nrfReady = initRadio();

    // Inicializa pacote nRF24 em estado neutro
    for (uint8_t i = 0; i < 8; i++) pkt.p[i] = 128;
    pkt.s1 = 0;
    pkt.s2 = 0;

    // Pequena pausa para estabilizar regulador 3.3V do Nano sob carga do nRF24
    delay(50);
    digitalWrite(PIN_LED, LOW);
    lastLoopMs = millis();
}

// =============================================================================
// LOOP PRINCIPAL — 50 Hz fixo
// =============================================================================
void loop() {
    const uint32_t now = millis();
    if (now - lastLoopMs < LOOP_INTERVAL_MS) return;
    lastLoopMs = now;

    // 1. Lê todos os inputs (pots + switches)
    readInputs();

    // 2. Emite via nRF24 (compat com RX antigos) — não bloqueia se rádio falhou
    if (nrfReady) {
        bool ok = radio.write(&pkt, sizeof(pkt));
        if (ok) txCountNrf++;
    }

    // 3. Monta e emite frame CRSF via HW Serial (vai pro Heltec V2 + ELRS)
    buildCrsfChannels();
    const size_t n = crsf_build_rc_frame(crsf_frame, crsf_channels);
    Serial.write(crsf_frame, n);
    txCountCrsf++;

    // 4. LED pisca a cada ~25 frames (~500 ms) para indicar "vida"
    if ((txCountCrsf & 0x1F) == 0) {
        digitalWrite(PIN_LED, !digitalRead(PIN_LED));
    }
}
