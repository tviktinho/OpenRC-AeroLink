/* =============================================================================
 * lora_link.h — Protocolo LoRa customizado do OpenRC AeroLink (Fase 2)
 *
 * Substitui o ELRS por um protocolo PRÓPRIO sobre LoRa raw. Mais simples,
 * sem FHSS, sem hardcodes de potência, controle TOTAL do nosso lado.
 *
 * FRAME LAYOUT (28 bytes total):
 *   ┌──────┬──────┬─────┬─────┬─────────────────────┬─────────┐
 *   │ STX1 │ STX2 │ LEN │ SEQ │ PAYLOAD (22 bytes) │ CRC16   │
 *   │ 0xC4 │ 0x71 │ 24  │ ++  │ 16 canais 11-bit   │ 2 bytes │
 *   └──────┴──────┴─────┴─────┴─────────────────────┴─────────┘
 *
 *   STX1, STX2: sync bytes (0xC4, 0x71 — escolhidos arbitrariamente)
 *   LEN: tamanho de [SEQ + PAYLOAD + CRC16] = 1 + 22 + 2 = 25 bytes
 *   SEQ: contador 0-255 que incrementa a cada frame (pra detectar perdas)
 *   PAYLOAD: 16 canais × 11 bits packed (mesmo formato do CRSF, 22 bytes)
 *   CRC16: CRC-16-CCITT (poly 0x1021, init 0xFFFF) sobre [SEQ + PAYLOAD]
 *
 * PARÂMETROS LoRa:
 *   Frequência: 915 MHz (FCC US/BR)
 *   Bandwidth:  500 kHz (taxa alta, menor alcance que 125kHz mas suficiente)
 *   Spread Factor: 7 (chirp mais rápido)
 *   Coding Rate: 4/5 (correção mínima, frame mais curto)
 *   Sync Word: 0x12 (padrão LoRa)
 *   TX Power: 20 dBm (100 mW, máximo do SX1276 sem PA externo)
 *
 * Com esses parâmetros:
 *   - Taxa efetiva ≈ 21.875 kbps
 *   - Time-on-air do nosso frame (28 bytes) ≈ 13 ms
 *   - Permite até ~76 Hz teóricos (vamos usar 50 Hz com folga)
 *
 * Por que não FHSS (frequency hopping)?
 *   Pra simplificar a v1. ELRS faz FHSS pra robustez contra interferência,
 *   mas adiciona complexidade considerável (sincronização entre TX e RX).
 *   Vamos rodar canal fixo primeiro, depois se for o caso adicionar FHSS.
 *
 * Compatibilidade:
 *   - Frame propositalmente parecido com CRSF — facilita reuso de código
 *   - O payload é IDÊNTICO ao do CRSF_FRAMETYPE_RC_CHANNELS_PACKED
 *   - Quem decodifica o nosso payload pode usar crsf_unpack_channels() direto
 *
 * Autor: OpenRC-AeroLink Fase 2 (protocolo próprio)
 * ============================================================================= */

#ifndef LORA_LINK_H
#define LORA_LINK_H

#include <Arduino.h>

// =============================================================================
// Constantes do protocolo
// =============================================================================
#define LORA_LINK_STX1                  0xC4
#define LORA_LINK_STX2                  0x71

#define LORA_LINK_PAYLOAD_BYTES         22       // mesmo do CRSF RC_CHANNELS_PACKED
#define LORA_LINK_FRAME_BYTES           (2 + 1 + 1 + LORA_LINK_PAYLOAD_BYTES + 2)
                                        // STX1 + STX2 + LEN + SEQ + payload + CRC16
                                        // = 2 + 1 + 1 + 22 + 2 = 28 bytes

// LEN é o tamanho a partir de SEQ (inclusivo) até CRC16 (inclusivo)
#define LORA_LINK_LEN_BYTE              (1 + LORA_LINK_PAYLOAD_BYTES + 2)
                                        // = 1 + 22 + 2 = 25

// =============================================================================
// Parâmetros LoRa (Heltec V2 SX1276)
// =============================================================================
#define LORA_LINK_FREQ_HZ               915000000UL   // 915 MHz (FCC)
#define LORA_LINK_BANDWIDTH_HZ          500000        // 500 kHz
#define LORA_LINK_SPREADING_FACTOR      7
#define LORA_LINK_CODING_RATE_DEN       5             // 4/5
#define LORA_LINK_SYNC_WORD             0x12          // padrão LoRa (não LoRaWAN)
#define LORA_LINK_TX_POWER_DBM          20            // 100 mW

// Periodicidade de envio
#define LORA_LINK_PERIOD_MS             20            // 50 Hz

// =============================================================================
// CRC-16-CCITT (poly 0x1021, init 0xFFFF, no reflection, xorout 0x0000)
// =============================================================================
static inline uint16_t lora_crc16(const uint8_t* data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

// =============================================================================
// Monta um frame LoRa pronto pra transmissão.
//
// `payload`: 22 bytes contendo 16 canais 11-bit packed (mesmo do CRSF).
//            Você pode usar o helper crsf_pack_channels() do crsf_tx.h
//            se quiser preencher manualmente.
// `frame`: buffer de saída (precisa ter pelo menos LORA_LINK_FRAME_BYTES = 28).
//
// Retorna: nº de bytes escritos em `frame` (sempre 28).
// =============================================================================
static inline size_t lora_link_build_frame(uint8_t frame[LORA_LINK_FRAME_BYTES],
                                           const uint8_t payload[LORA_LINK_PAYLOAD_BYTES]) {
    static uint8_t seq = 0;

    frame[0] = LORA_LINK_STX1;
    frame[1] = LORA_LINK_STX2;
    frame[2] = LORA_LINK_LEN_BYTE;
    frame[3] = seq++;
    memcpy(&frame[4], payload, LORA_LINK_PAYLOAD_BYTES);

    // CRC16 sobre SEQ + PAYLOAD (NÃO inclui STX nem LEN)
    uint16_t crc = lora_crc16(&frame[3], 1 + LORA_LINK_PAYLOAD_BYTES);
    frame[4 + LORA_LINK_PAYLOAD_BYTES]     = (uint8_t)(crc >> 8);   // CRC high byte
    frame[4 + LORA_LINK_PAYLOAD_BYTES + 1] = (uint8_t)(crc & 0xFF); // CRC low byte

    return LORA_LINK_FRAME_BYTES;
}

// =============================================================================
// Parser RX (espelho do build) — implementado no lado RX, mas declarado aqui
// pra completude. State machine simples: WAIT_STX1 → WAIT_STX2 → WAIT_LEN →
// COLLECT → valida CRC → entrega payload.
//
// Será usado no firmware do RX customizado (próxima fase).
// =============================================================================

#endif // LORA_LINK_H
