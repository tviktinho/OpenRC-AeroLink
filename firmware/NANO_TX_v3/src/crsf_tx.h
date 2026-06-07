/* =============================================================================
 * crsf_tx.h  —  Encoder CRSF (TBS Crossfire) leve para Arduino Nano (ATmega328P)
 *
 * CÓPIA IDÊNTICA do firmware/TX_NANO_v2/crsf_tx.h — mantido aqui pra que o v3
 * seja self-contained (PlatformIO compila a pasta src/ inteira). Se você
 * atualizar o crsf_tx.h do v2, atualize esta cópia também.
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
 * Baud rate CRSF:
 *   400 000 baud — o ATmega328P @ 16 MHz gera 400 000 com 0 % de erro
 *   (UBRR=4, U2X=1). Pedir 420 000 cai em 400 000 com 4,76 % de erro,
 *   perigoso para receivers exigentes. ELRS v3+ aceita 400000.
 * ============================================================================= */

#ifndef CRSF_TX_H
#define CRSF_TX_H

#include <Arduino.h>

// =============================================================================
// Constantes do protocolo CRSF
// =============================================================================
#define CRSF_SYNC_BYTE                 0xC8
#define CRSF_FRAMETYPE_RC_CHANNELS_PACKED 0x16

#define CRSF_NUM_CHANNELS              16
#define CRSF_CHANNEL_VALUE_MIN         172
#define CRSF_CHANNEL_VALUE_MID         992
#define CRSF_CHANNEL_VALUE_MAX         1811

#define CRSF_PAYLOAD_SIZE_RC           22
#define CRSF_FRAME_SIZE                (1 + 1 + 1 + CRSF_PAYLOAD_SIZE_RC + 1)  // 26

#define CRSF_BAUDRATE                  400000UL

// =============================================================================
// CRC8 — polinômio 0xD5 (padrão CRSF, versão por bits pra caber em qualquer Nano)
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

// byte 0..255 -> CRSF 172..1811 (mapeamento usado pelo OpenRC-AeroLink)
static inline uint16_t crsf_byte_to_channel(uint8_t b) {
    return (uint16_t)(CRSF_CHANNEL_VALUE_MIN
                      + (((uint32_t)b * (CRSF_CHANNEL_VALUE_MAX - CRSF_CHANNEL_VALUE_MIN))
                         + 127) / 255);
}

// switch on/off -> CRSF low/high
static inline uint16_t crsf_sw_to_channel(uint8_t on) {
    return on ? CRSF_CHANNEL_VALUE_MAX : CRSF_CHANNEL_VALUE_MIN;
}

// =============================================================================
// Empacotamento: 16 canais 11-bit -> 22 bytes (LSB first)
// =============================================================================
static inline void crsf_pack_channels(const uint16_t channels[CRSF_NUM_CHANNELS],
                                      uint8_t out[CRSF_PAYLOAD_SIZE_RC]) {
    uint32_t buf = 0;
    uint8_t bits = 0;
    uint8_t idx  = 0;

    for (uint8_t ch = 0; ch < CRSF_NUM_CHANNELS; ch++) {
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
    if (bits > 0 && idx < CRSF_PAYLOAD_SIZE_RC) {
        out[idx] = (uint8_t)(buf & 0xFF);
    }
}

// =============================================================================
// Monta o frame CRSF completo. Retorna CRSF_FRAME_SIZE (26).
// =============================================================================
static inline size_t crsf_build_rc_frame(uint8_t frame[CRSF_FRAME_SIZE],
                                         const uint16_t channels[CRSF_NUM_CHANNELS]) {
    frame[0] = CRSF_SYNC_BYTE;
    frame[1] = CRSF_PAYLOAD_SIZE_RC + 2;                  // LEN = 24
    frame[2] = CRSF_FRAMETYPE_RC_CHANNELS_PACKED;
    crsf_pack_channels(channels, &frame[3]);
    frame[3 + CRSF_PAYLOAD_SIZE_RC] = crsf_crc8(&frame[2], 1 + CRSF_PAYLOAD_SIZE_RC);
    return CRSF_FRAME_SIZE;
}

#endif // CRSF_TX_H
