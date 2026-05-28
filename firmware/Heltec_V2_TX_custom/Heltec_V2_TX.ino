/* =============================================================================
 * Heltec_V2_TX.ino — Transmissor LoRa CUSTOMIZADO (Fase 2)
 * OpenRC AeroLink — protocolo próprio (não-ELRS)
 *
 * Substitui o ELRS oficial por um firmware nosso que:
 *   1. Recebe CRSF do NANO_TX_v2 na UART2 (GPIO 17 @ 400000 baud)
 *   2. Empacota os 16 canais 11-bit em frame próprio (lora_link.h)
 *   3. Transmite via SX1276 em 915 MHz, 100 mW REAIS, 50 Hz
 *
 * SEM ELRS:
 *   - Sem max-power hardcoded em 1 mW
 *   - Sem bugs de pré-release
 *   - Sem dependência de Custom Hardware Layout
 *   - Controle total do protocolo
 *
 * COMPATIBILIDADE:
 *   - Nano TX v2 não muda (já manda CRSF)
 *   - ESP32 FC v2 NÃO funciona com isso (espera CRSF, não LoRa direto)
 *     → vai precisar de RX customizado correspondente (Fase B do roadmap)
 *
 * PINOUT do Heltec V2 (mesmo do hardware.json do ELRS):
 *   SX1276 SPI:  SCK=5, MISO=19, MOSI=27, NSS=18, RST=14, DIO0=26
 *   UART2 CRSF:  RX=17 (vem do Nano TX0 via divisor 3k3/1k8)
 *                TX=23 (telemetria futura, não usado agora)
 *   OLED I2C:    SDA=4, SCL=15, RST=16 (usado pra status)
 *   LED:         GPIO 25 (LED branco on-board)
 *
 * DEPENDÊNCIAS (PlatformIO):
 *   sandeepmistry/LoRa @ ^0.8.0
 *   ThingPulse/ESP8266 and ESP32 OLED driver for SSD1306 displays
 *     (pra mostrar status, opcional)
 *
 * Autor: OpenRC-AeroLink Fase 2
 * ============================================================================= */

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include "crsf_rx.h"
#include "lora_link.h"

// =============================================================================
// PINAGEM (Heltec V2)
// =============================================================================
// SX1276 SPI (fixos do Heltec V2)
constexpr uint8_t PIN_LORA_SCK   = 5;
constexpr uint8_t PIN_LORA_MISO  = 19;
constexpr uint8_t PIN_LORA_MOSI  = 27;
constexpr uint8_t PIN_LORA_NSS   = 18;
constexpr uint8_t PIN_LORA_RST   = 14;
constexpr uint8_t PIN_LORA_DIO0  = 26;

// UART2 CRSF do Nano (remapeada — pinos default 16/17 são usados pra OLED)
constexpr uint8_t PIN_CRSF_RX    = 17;   // ← TX0 do Nano (via divisor)
constexpr uint8_t PIN_CRSF_TX    = 23;   // → telemetria pro Nano (futuro)

// LED on-board pra indicar atividade
constexpr uint8_t PIN_LED        = 25;

// =============================================================================
// OBJETOS GLOBAIS
// =============================================================================
CrsfRx crsf;

// Buffer do frame LoRa pronto pra TX
uint8_t lora_frame[LORA_LINK_FRAME_BYTES];

// Estatísticas
uint32_t tx_count = 0;
uint32_t last_send_ms = 0;
uint32_t last_print_ms = 0;

// =============================================================================
// SETUP
// =============================================================================
void setup() {
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, HIGH);  // acende durante init

    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println(F("==================================="));
    Serial.println(F("  Heltec V2 TX — Protocolo Custom"));
    Serial.println(F("  OpenRC AeroLink Fase 2"));
    Serial.println(F("==================================="));

    // -------------------- CRSF UART ---------------------
    Serial2.begin(CRSF_BAUDRATE, SERIAL_8N1, PIN_CRSF_RX, PIN_CRSF_TX);
    Serial.printf("CRSF UART2: RX=GPIO%d TX=GPIO%d @ %lu baud\n",
                  PIN_CRSF_RX, PIN_CRSF_TX, (unsigned long)CRSF_BAUDRATE);

    // -------------------- SX1276 LoRa --------------------
    // ESP32 precisa de SPI customizado pra pinos não-default
    SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);
    LoRa.setPins(PIN_LORA_NSS, PIN_LORA_RST, PIN_LORA_DIO0);

    Serial.printf("LoRa init em %lu Hz... ", LORA_LINK_FREQ_HZ);
    if (!LoRa.begin(LORA_LINK_FREQ_HZ)) {
        Serial.println(F("FALHA!"));
        Serial.println(F("Cheque pinos SPI do SX1276."));
        while (1) {
            digitalWrite(PIN_LED, !digitalRead(PIN_LED));
            delay(100);  // pisca rápido se LoRa falhar
        }
    }
    Serial.println(F("OK"));

    LoRa.setSpreadingFactor(LORA_LINK_SPREADING_FACTOR);
    LoRa.setSignalBandwidth(LORA_LINK_BANDWIDTH_HZ);
    LoRa.setCodingRate4(LORA_LINK_CODING_RATE_DEN);
    LoRa.setSyncWord(LORA_LINK_SYNC_WORD);
    LoRa.setTxPower(LORA_LINK_TX_POWER_DBM);  // 20 dBm = 100 mW (PA_BOOST)
    LoRa.enableCrc();   // CRC interno do LoRa além do nosso CRC16

    Serial.printf("LoRa configurado: SF%d BW%d CR4/%d PWR=%d dBm SYNC=0x%02X\n",
                  LORA_LINK_SPREADING_FACTOR,
                  LORA_LINK_BANDWIDTH_HZ,
                  LORA_LINK_CODING_RATE_DEN,
                  LORA_LINK_TX_POWER_DBM,
                  LORA_LINK_SYNC_WORD);
    Serial.printf("Frame: %d bytes @ %d Hz = %d bytes/s\n",
                  LORA_LINK_FRAME_BYTES,
                  1000 / LORA_LINK_PERIOD_MS,
                  LORA_LINK_FRAME_BYTES * (1000 / LORA_LINK_PERIOD_MS));

    digitalWrite(PIN_LED, LOW);
    last_send_ms = millis();
    Serial.println(F("Aguardando CRSF do Nano TX..."));
}

// =============================================================================
// LOOP
// =============================================================================
void loop() {
    // 1. Alimenta o parser CRSF com tudo que chega da UART
    while (Serial2.available()) {
        crsf.feed((uint8_t)Serial2.read());
    }

    // 2. Envia frame LoRa a cada LORA_LINK_PERIOD_MS (50 Hz)
    uint32_t now = millis();
    if (now - last_send_ms >= LORA_LINK_PERIOD_MS) {
        last_send_ms = now;

        // Só envia se temos dados CRSF recentes (último frame em <500 ms)
        bool crsf_alive = (now - crsf.lastFrameMs() < 500);

        if (crsf_alive) {
            // Pega os 16 canais do parser CRSF e re-empacota em 11-bit
            // (usamos o mesmo formato CRSF pra payload — 22 bytes packed)
            uint16_t channels[CRSF_NUM_CHANNELS];
            for (uint8_t i = 0; i < CRSF_NUM_CHANNELS; i++) {
                channels[i] = crsf.getChannel(i);
            }

            // Pack 16×11-bit em 22 bytes (mesmo algoritmo do crsf_tx.h)
            uint8_t packed[LORA_LINK_PAYLOAD_BYTES];
            uint32_t buf = 0;
            uint8_t  bits = 0;
            uint8_t  idx  = 0;
            for (uint8_t ch = 0; ch < CRSF_NUM_CHANNELS; ch++) {
                uint16_t v = channels[ch] & 0x7FF;
                buf |= ((uint32_t)v) << bits;
                bits += 11;
                while (bits >= 8 && idx < LORA_LINK_PAYLOAD_BYTES) {
                    packed[idx++] = (uint8_t)(buf & 0xFF);
                    buf >>= 8;
                    bits -= 8;
                }
            }
            if (bits > 0 && idx < LORA_LINK_PAYLOAD_BYTES) {
                packed[idx] = (uint8_t)(buf & 0xFF);
            }

            // Monta o frame LoRa e transmite
            lora_link_build_frame(lora_frame, packed);
            LoRa.beginPacket();
            LoRa.write(lora_frame, LORA_LINK_FRAME_BYTES);
            LoRa.endPacket();   // bloqueia até terminar TX (~13 ms)

            tx_count++;

            // LED pisca a cada 25 frames (~500 ms) — vida
            if ((tx_count & 0x1F) == 0) {
                digitalWrite(PIN_LED, !digitalRead(PIN_LED));
            }
        } else {
            // Sem CRSF — não transmite, mas mantém LED apagado
            digitalWrite(PIN_LED, LOW);
        }
    }

    // 3. Debug a cada 1 segundo
    if (now - last_print_ms >= 1000) {
        last_print_ms = now;
        bool alive = (now - crsf.lastFrameMs() < 500);
        Serial.printf("[TX] tx=%lu | CRSF ok=%lu bad=%lu %s | CH1=%4u CH2=%4u CH3=%4u CH4=%4u\n",
                      (unsigned long)tx_count,
                      (unsigned long)crsf.goodFrames(),
                      (unsigned long)crsf.badFrames(),
                      alive ? "LIVE" : "DEAD",
                      crsf.getChannel(0),
                      crsf.getChannel(1),
                      crsf.getChannel(2),
                      crsf.getChannel(3));
    }
}
