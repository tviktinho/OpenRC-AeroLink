/* =============================================================================
 * crsf_tx.h  —  Encoder CRSF (TBS Crossfire) leve para Arduino Nano (ATmega328P)
 *
 * Implementa SOMENTE o lado TX (envio) do frame CRSF_FRAMETYPE_RC_CHANNELS_PACKED,
 * que é o que o módulo ELRS (no Heltec V2) precisa para gerar o link.
 *
 * Protocolo CRSF (resumo):
 *   Frame layout: [SYNC] [LEN] [TYPE] [PAYLOAD ...] [CRC8]
 *     SYNC    = 0xC8  (endereço destino "Flight Controller", padrão p/ módulo TX)
 *     LEN     = nº de bytes a partir de TYPE até CRC inclusive (= payload + 2)
 *     TYPE    = 0x16  (CRSF_FRAMETYPE_RC_CHANNELS_PACKED)
 *     PAYLOAD = 22 bytes — 16 canais empacotados em 11 bits cada (LSB first)
 *     CRC8    = polinômio 0xD5, calculado sobre [TYPE + PAYLOAD]
 *
 * Frame completo = 26 bytes (SYNC + LEN + TYPE + 22 + CRC = 1+1+1+22+1).
 *
 * Cada canal CRSF é um inteiro 11-bit (0..2047) com convenção:
 *   172   = 988  µs   (1000 µs equivalente, mínimo)
 *   992   = 1500 µs   (centro)
 *   1811  = 2012 µs   (máximo)
 *
 * Helpers `crsf_byte_to_channel(b)` e `crsf_us_to_channel(us)` fazem o mapeamento.
 *
 * Baud rate CRSF:
 *   CRSF "clássico" é 420 000 baud. PORÉM o ATmega328P @ 16 MHz NÃO consegue
 *   gerar 420 000 de forma precisa: com U2X=1 e UBRR=4, a UART produz
 *   16 000 000 / (8 * 5) = 400 000 baud exatos (0 % de erro). Pedir 420 000
 *   no Serial.begin() faz o Arduino arredondar pra UBRR=4 e gerar 400 000
 *   mesmo assim, mas com 4,76 % de erro relativo — alto demais p/ algumas
 *   implementações CRSF.
 *
 *   Solução: usar 400 000 baud, que o AVR gera com 0 % de erro, e configurar
 *   o módulo ELRS (no Heltec) p/ aceitar esse rate. ELRS v3.x+ tem detecção
 *   automática de baud rate; v4.x permite configurar manualmente no painel
 *   web (/hardware.html → "Serial Baud Rate"). Em ESP32 não há essa limitação
 *   — 420 000 é gerado limpo se você quiser usar do lado do FC.
 *
 * Uso típico:
 *   #include "crsf_tx.h"
 *   uint16_t ch[16];
 *   // preencher ch[0..15] com valores 0..255 (byte) ou já 172..1811 (CRSF)
 *   uint8_t frame[CRSF_FRAME_SIZE];
 *   size_t  n = crsf_build_rc_frame(frame, ch);
 *   Serial.write(frame, n);   // HW Serial @ 420000 baud
 *
 * Autor: OpenRC-AeroLink, upgrade ELRS (Fase 1)
 * ============================================================================= */

#ifndef CRSF_TX_H
#define CRSF_TX_H

#include <Arduino.h>

// =============================================================================
// Constantes do protocolo CRSF
// =============================================================================
#define CRSF_SYNC_BYTE                 0xC8   // endereço "Flight Controller"
#define CRSF_FRAMETYPE_RC_CHANNELS_PACKED 0x16

#define CRSF_NUM_CHANNELS              16
#define CRSF_CHANNEL_VALUE_MIN         172    // 988  µs
#define CRSF_CHANNEL_VALUE_MID         992    // 1500 µs
#define CRSF_CHANNEL_VALUE_MAX         1811   // 2012 µs

// Tamanhos
#define CRSF_PAYLOAD_SIZE_RC           22     // 16 canais * 11 bits / 8 = 22 bytes
#define CRSF_FRAME_SIZE                (1 + 1 + 1 + CRSF_PAYLOAD_SIZE_RC + 1)  // = 26

// Baud rate CRSF — 400 000 (e não 420 000) porque o ATmega328P @ 16 MHz gera
// exatamente 400 000 baud com 0 % de erro. Pedir 420 000 cairia em 400 000
// real com 4,76 % de erro, perigoso para receivers exigentes.
// Veja nota técnica completa no cabeçalho do arquivo.
#define CRSF_BAUDRATE                  400000UL

// =============================================================================
// CRC8 — polinômio 0xD5 (padrão CRSF, tabela lookup é mais rápida porém custa
// 256 bytes de flash; aqui usamos versão por bits para caber em qualquer Nano)
// =============================================================================
static inline uint8_t crsf_crc8(const uint8_t* data, uint8_t len) {
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0xD5) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

// =============================================================================
// Helpers de conversão
// =============================================================================

// Converte um byte 0..255 (igual ao mapeamento usado no firmware antigo dos pots)
// para a escala CRSF 172..1811. Útil para reaproveitar o `pkt.p[i]` do v1.
static inline uint16_t crsf_byte_to_channel(uint8_t b) {
    // map linear: 0 -> 172, 255 -> 1811
    // (1811 - 172) = 1639; usamos cálculo em uint32 para evitar overflow
    return (uint16_t)(CRSF_CHANNEL_VALUE_MIN
                      + (((uint32_t)b * (CRSF_CHANNEL_VALUE_MAX - CRSF_CHANNEL_VALUE_MIN))
                         + 127) / 255);
}

// Converte um pulse-width em microsegundos (1000..2000) para CRSF (172..1811).
// Saturação automática fora da faixa.
static inline uint16_t crsf_us_to_channel(uint16_t us) {
    if (us < 988)  us = 988;
    if (us > 2012) us = 2012;
    // 988..2012  -> 172..1811   (faixa "padrão" do CRSF, ~1024 unidades / ~1024 us)
    return (uint16_t)(((uint32_t)(us - 988) * (CRSF_CHANNEL_VALUE_MAX - CRSF_CHANNEL_VALUE_MIN)
                       + 512) / (2012 - 988) + CRSF_CHANNEL_VALUE_MIN);
}

// Compacta um switch on/off (0 ou 1) para um canal CRSF (low/high).
static inline uint16_t crsf_sw_to_channel(uint8_t on) {
    return on ? CRSF_CHANNEL_VALUE_MAX : CRSF_CHANNEL_VALUE_MIN;
}

// =============================================================================
// Empacotamento: 16 canais 11-bit -> 22 bytes (LSB first)
//
// Layout (bit-stream contínuo, little-endian por canal):
//   byte[0] = ch0[0..7]
//   byte[1] = (ch1[0..4] << 3) | (ch0[8..10])
//   byte[2] = ch1[5..10] << 0   ...
//   ...
// Implementação usa shift acumulado num uint32 para simplificar.
// =============================================================================
static inline void crsf_pack_channels(const uint16_t channels[CRSF_NUM_CHANNELS],
                                      uint8_t out[CRSF_PAYLOAD_SIZE_RC]) {
    uint32_t buf = 0;
    uint8_t bits = 0;
    uint8_t idx  = 0;

    for (uint8_t ch = 0; ch < CRSF_NUM_CHANNELS; ch++) {
        // satura 11 bits
        uint16_t v = channels[ch];
        if (v > 0x7FF) v = 0x7FF;
        buf |= ((uint32_t)v) << bits;
        bits += 11;
        while (bits >= 8 && idx < CRSF_PAYLOAD_SIZE_RC) {
            out[idx++] = (uint8_t)(buf & 0xFF);
            buf >>= 8;
            bits -= 8;
        }
    }
    // resto (não deve ocorrer com 16 canais * 11 = 176 bits = 22 bytes exatos)
    if (bits > 0 && idx < CRSF_PAYLOAD_SIZE_RC) {
        out[idx] = (uint8_t)(buf & 0xFF);
    }
}

// =============================================================================
// Monta o frame CRSF completo para envio.
// Retorna o nº de bytes escritos em `frame` (sempre CRSF_FRAME_SIZE).
//
// `channels` = 16 valores em escala CRSF (172..1811). Use os helpers acima.
// =============================================================================
static inline size_t crsf_build_rc_frame(uint8_t frame[CRSF_FRAME_SIZE],
                                         const uint16_t channels[CRSF_NUM_CHANNELS]) {
    frame[0] = CRSF_SYNC_BYTE;                            // 0xC8
    frame[1] = CRSF_PAYLOAD_SIZE_RC + 2;                  // LEN = payload + TYPE + CRC = 24
    frame[2] = CRSF_FRAMETYPE_RC_CHANNELS_PACKED;         // 0x16
    crsf_pack_channels(channels, &frame[3]);              // 22 bytes de payload
    frame[3 + CRSF_PAYLOAD_SIZE_RC] = crsf_crc8(&frame[2], 1 + CRSF_PAYLOAD_SIZE_RC); // CRC sobre TYPE+PAYLOAD

    return CRSF_FRAME_SIZE;
}

#endif // CRSF_TX_H
