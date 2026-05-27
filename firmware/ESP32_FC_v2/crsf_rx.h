/* =============================================================================
 * crsf_rx.h  —  Parser CRSF (TBS Crossfire / ELRS) lado RX para ESP32
 *
 * Espelho do crsf_tx.h do NANO_TX_v2. Aqui implementamos o lado de RECEPÇÃO
 * do frame CRSF_FRAMETYPE_RC_CHANNELS_PACKED (0x16), que é o que o RX ELRS
 * emite continuamente para o FC.
 *
 * Protocolo (resumo, igual ao crsf_tx.h):
 *   Frame: [SYNC] [LEN] [TYPE] [PAYLOAD (22B)] [CRC8]
 *     SYNC    = 0xC8  (endereço destino "Flight Controller")
 *     LEN     = 24    (TYPE + 22 PAYLOAD + 1 CRC)
 *     TYPE    = 0x16  (RC_CHANNELS_PACKED)
 *     PAYLOAD = 16 canais × 11 bits empacotados (LSB first)
 *     CRC8    = polinômio 0xD5, sobre TYPE + PAYLOAD
 *
 * Uso típico:
 *   #include "crsf_rx.h"
 *   CrsfRx crsf;
 *   void setup() { Serial2.begin(420000, SERIAL_8N1, RX_PIN, TX_PIN); }
 *   void loop() {
 *     while (Serial2.available()) crsf.feed(Serial2.read());
 *     if (crsf.hasNewFrame()) {
 *       uint16_t ch1 = crsf.getChannel(0);  // 172..1811
 *       uint16_t us  = crsf_channel_to_us(ch1);  // 988..2012
 *       ...
 *       crsf.clearFrameFlag();
 *     }
 *     if (millis() - crsf.lastFrameMs() > 200) {  // failsafe
 *       ...
 *     }
 *   }
 *
 * Autor: OpenRC-AeroLink, upgrade ELRS (Fase 1)
 * ============================================================================= */

#ifndef CRSF_RX_H
#define CRSF_RX_H

#include <Arduino.h>

// =============================================================================
// Constantes do protocolo CRSF (mesmas do crsf_tx.h, repetidas aqui para
// evitar coupling entre as pastas do TX e do FC)
// =============================================================================
#define CRSF_SYNC_BYTE_FC              0xC8   // SYNC quando destinatário é FC
#define CRSF_FRAMETYPE_RC_CHANNELS_PACKED 0x16

#define CRSF_NUM_CHANNELS              16
#define CRSF_CHANNEL_VALUE_MIN         172    // 988  µs
#define CRSF_CHANNEL_VALUE_MID         992    // 1500 µs
#define CRSF_CHANNEL_VALUE_MAX         1811   // 2012 µs

#define CRSF_PAYLOAD_SIZE_RC           22
#define CRSF_FRAME_SIZE                26     // SYNC+LEN+TYPE+22+CRC

#define CRSF_BAUDRATE                  420000UL

// Limites razoáveis para LEN (sanity check do parser)
#define CRSF_LEN_MIN                   2
#define CRSF_LEN_MAX                   62

// =============================================================================
// CRC8 — mesmo polinômio do TX (0xD5)
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
// Helpers de conversão (mesmo escopo do TX, mas em sentido inverso)
// =============================================================================

// CRSF (172..1811) -> µs (988..2012)  — escala linear, satura nos extremos
static inline uint16_t crsf_channel_to_us(uint16_t ch) {
    if (ch < CRSF_CHANNEL_VALUE_MIN) ch = CRSF_CHANNEL_VALUE_MIN;
    if (ch > CRSF_CHANNEL_VALUE_MAX) ch = CRSF_CHANNEL_VALUE_MAX;
    // (1811 - 172) = 1639  ->  (2012 - 988) = 1024
    return (uint16_t)(988 + ((uint32_t)(ch - CRSF_CHANNEL_VALUE_MIN) * 1024UL + 819) / 1639UL);
}

// CRSF (172..1811) -> byte 0..255   (mesmo mapeamento que crsf_tx.h faz no
// sentido inverso; usado se quisermos reaproveitar lógica do firmware nRF24 antigo)
static inline uint8_t crsf_channel_to_byte(uint16_t ch) {
    if (ch < CRSF_CHANNEL_VALUE_MIN) ch = CRSF_CHANNEL_VALUE_MIN;
    if (ch > CRSF_CHANNEL_VALUE_MAX) ch = CRSF_CHANNEL_VALUE_MAX;
    return (uint8_t)(((uint32_t)(ch - CRSF_CHANNEL_VALUE_MIN) * 255UL + 819) / 1639UL);
}

// CH8 bitmask: o NANO_TX_v2 empacota 4 switches em 16 níveis no CH8.
// Esta função reverte: ch -> bitmask 0..15  ->  bit0=SW1, bit1=SW2, bit2=SW3, bit3=SW4.
static inline uint8_t crsf_ch8_to_switches(uint16_t ch) {
    if (ch < CRSF_CHANNEL_VALUE_MIN) ch = CRSF_CHANNEL_VALUE_MIN;
    if (ch > CRSF_CHANNEL_VALUE_MAX) ch = CRSF_CHANNEL_VALUE_MAX;
    // 16 níveis (0..15) dentro de 172..1811
    uint16_t level = (uint16_t)(((uint32_t)(ch - CRSF_CHANNEL_VALUE_MIN) * 15UL + 819) / 1639UL);
    if (level > 15) level = 15;
    return (uint8_t)level;
}

// =============================================================================
// Unpacking: 22 bytes -> 16 canais 11-bit
// =============================================================================
static inline void crsf_unpack_channels(const uint8_t payload[CRSF_PAYLOAD_SIZE_RC],
                                        uint16_t out[CRSF_NUM_CHANNELS]) {
    uint32_t buf = 0;
    uint8_t  bits = 0;
    uint8_t  idx  = 0;

    for (uint8_t ch = 0; ch < CRSF_NUM_CHANNELS; ch++) {
        while (bits < 11 && idx < CRSF_PAYLOAD_SIZE_RC) {
            buf |= ((uint32_t)payload[idx++]) << bits;
            bits += 8;
        }
        out[ch] = (uint16_t)(buf & 0x7FF);
        buf >>= 11;
        bits -= 11;
    }
}

// =============================================================================
// Parser state machine (byte-a-byte, sem alocação dinâmica, reentrante)
// =============================================================================
class CrsfRx {
public:
    CrsfRx() : state_(WAIT_SYNC), len_(0), idx_(0),
               new_frame_(false), last_frame_ms_(0), bad_frames_(0), good_frames_(0) {
        for (uint8_t i = 0; i < CRSF_NUM_CHANNELS; i++) channels_[i] = CRSF_CHANNEL_VALUE_MID;
    }

    // Alimenta o parser com um byte vindo da UART. Retorna true se um frame
    // RC_CHANNELS_PACKED válido acabou de ser decodificado neste byte.
    bool feed(uint8_t b) {
        switch (state_) {
            case WAIT_SYNC:
                if (b == CRSF_SYNC_BYTE_FC) {
                    state_ = WAIT_LEN;
                }
                return false;

            case WAIT_LEN:
                if (b < CRSF_LEN_MIN || b > CRSF_LEN_MAX) {
                    // LEN absurdo, descarta e volta a procurar sync
                    state_ = WAIT_SYNC;
                    bad_frames_++;
                    return false;
                }
                len_ = b;          // LEN = TYPE + PAYLOAD + CRC
                idx_ = 0;
                state_ = COLLECT_PAYLOAD;
                return false;

            case COLLECT_PAYLOAD:
                buf_[idx_++] = b;
                if (idx_ >= len_) {
                    // último byte é CRC; os primeiros (len_ - 1) são TYPE + PAYLOAD
                    uint8_t calc = crsf_crc8(buf_, len_ - 1);
                    uint8_t recv = buf_[len_ - 1];
                    state_ = WAIT_SYNC;
                    if (calc != recv) {
                        bad_frames_++;
                        return false;
                    }
                    good_frames_++;
                    // Só interessa RC_CHANNELS_PACKED nesta fase
                    if (buf_[0] == CRSF_FRAMETYPE_RC_CHANNELS_PACKED
                        && (len_ - 2) == CRSF_PAYLOAD_SIZE_RC) {
                        crsf_unpack_channels(&buf_[1], channels_);
                        new_frame_     = true;
                        last_frame_ms_ = millis();
                        return true;
                    }
                    return false;
                }
                return false;
        }
        return false;
    }

    // Getters
    inline uint16_t getChannel(uint8_t idx) const {
        return (idx < CRSF_NUM_CHANNELS) ? channels_[idx] : CRSF_CHANNEL_VALUE_MID;
    }
    inline bool     hasNewFrame()  const { return new_frame_; }
    inline void     clearFrameFlag()     { new_frame_ = false; }
    inline uint32_t lastFrameMs()  const { return last_frame_ms_; }
    inline uint32_t goodFrames()   const { return good_frames_; }
    inline uint32_t badFrames()    const { return bad_frames_; }

private:
    enum State : uint8_t { WAIT_SYNC, WAIT_LEN, COLLECT_PAYLOAD };

    State    state_;
    uint8_t  len_;
    uint8_t  idx_;
    uint8_t  buf_[CRSF_LEN_MAX];           // TYPE + PAYLOAD + CRC

    uint16_t channels_[CRSF_NUM_CHANNELS];
    bool     new_frame_;
    uint32_t last_frame_ms_;
    uint32_t bad_frames_;
    uint32_t good_frames_;
};

#endif // CRSF_RX_H
