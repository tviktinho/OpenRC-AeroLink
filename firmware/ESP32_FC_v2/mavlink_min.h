/* =============================================================================
 * mavlink_min.h  —  Implementação minimalista MAVLink v1 para o ESP32 FC v2
 *
 * Pequena reimplementação manual do protocolo MAVLink v1 cobrindo apenas o que
 * precisamos para o Mission Planner reconhecer o nosso ESP32 como "vehicle" e
 * exibir os RC inputs no painel "Radio Calibration".
 *
 * Por que não usar a biblioteca oficial?
 *   * A pasta `firmware/ESP32/mavlink/` do legacy tem ~100 headers (~600 KB)
 *   * Pra um Hello-World do MAVLink, isso é overkill
 *   * Implementar à mão deixa o caminho do dado explícito → mais didático p/ TCC
 *   * Quando precisarmos de mais messages (ATTITUDE, GPS, etc.) podemos:
 *       (a) estender este arquivo
 *       (b) ou migrar para a lib oficial (refator de ~50 linhas)
 *
 * Frame MAVLink v1 (até 263 bytes total):
 *   ┌──────┬─────┬─────┬─────┬──────┬─────┬───────────┬─────────┐
 *   │ STX  │ LEN │ SEQ │ SYS │ COMP │ MSG │ PAYLOAD   │ CRC LH  │
 *   │ 0xFE │ N   │     │     │      │     │ N bytes   │ 2 bytes │
 *   └──────┴─────┴─────┴─────┴──────┴─────┴───────────┴─────────┘
 *
 *   STX  = 0xFE (MAVLink v1)
 *   LEN  = nº de bytes do PAYLOAD
 *   SEQ  = contador 0-255 que incrementa a cada frame enviado
 *   SYS  = system id (escolha qualquer 1-255; usamos 1)
 *   COMP = component id (1 = autopilot, 200 = telem radio, etc.)
 *   MSG  = message id (0 = HEARTBEAT, 35 = RC_CHANNELS_RAW, ...)
 *   CRC  = X.25 CRC16 sobre [LEN, SEQ, SYS, COMP, MSG, PAYLOAD, CRC_EXTRA]
 *          onde CRC_EXTRA é uma seed por message id (gerada do MD5 do XML)
 *
 * Refs:
 *   https://mavlink.io/en/guide/serialization.html
 *   https://github.com/mavlink/c_library_v1
 *
 * Autor: OpenRC-AeroLink, upgrade ELRS (Fase 1, MAVLink p/ Mission Planner)
 * ============================================================================= */

#ifndef MAVLINK_MIN_H
#define MAVLINK_MIN_H

#include <Arduino.h>

// =============================================================================
// Constantes MAVLink v1
// =============================================================================
#define MAVLINK_STX_V1                  0xFE

// IDs de Message (subset)
#define MAVLINK_MSG_ID_HEARTBEAT        0
#define MAVLINK_MSG_ID_RC_CHANNELS_RAW  35

// CRC_EXTRA por message id — gerado pelo MAVLink generator a partir do XML.
// Estes valores são fixos universalmente (mesmo CRC em qualquer dialeto):
//   HEARTBEAT       → 50
//   RC_CHANNELS_RAW → 244
#define MAVLINK_CRC_HEARTBEAT           50
#define MAVLINK_CRC_RC_CHANNELS_RAW     244

// Tamanhos de payload (v1, sem extensões)
#define MAVLINK_PAYLOAD_LEN_HEARTBEAT        9
#define MAVLINK_PAYLOAD_LEN_RC_CHANNELS_RAW  22

// MAV_TYPE (enum) — só os que importam pra nós
#define MAV_TYPE_GENERIC                 0
#define MAV_TYPE_FIXED_WING              1

// MAV_AUTOPILOT
#define MAV_AUTOPILOT_GENERIC            0

// MAV_MODE_FLAG (bits)
#define MAV_MODE_FLAG_SAFETY_ARMED       128
#define MAV_MODE_FLAG_MANUAL_INPUT_ENABLED 64

// MAV_STATE
#define MAV_STATE_UNINIT                 0
#define MAV_STATE_BOOT                   1
#define MAV_STATE_STANDBY                3
#define MAV_STATE_ACTIVE                 4

// Identidade do "vehicle" (escolha arbitrária)
#define MAV_SYSTEM_ID                    1
#define MAV_COMPONENT_ID                 1   // 1 = MAV_COMP_ID_AUTOPILOT1

// =============================================================================
// CRC X.25 (CCITT) — implementação little-endian usada pelo MAVLink
// =============================================================================
static inline void mav_crc_accumulate(uint8_t data, uint16_t *crc) {
    uint8_t tmp = data ^ (uint8_t)(*crc & 0xFF);
    tmp ^= (tmp << 4);
    *crc = (*crc >> 8) ^ ((uint16_t)tmp << 8) ^ ((uint16_t)tmp << 3) ^ ((uint16_t)tmp >> 4);
}

static inline void mav_crc_init(uint16_t *crc) {
    *crc = 0xFFFF;
}

// =============================================================================
// Helper interno: empacota e envia um frame MAVLink v1 completo
// (LEN ≤ 255, PAYLOAD pré-montado)
// =============================================================================
static inline void mav_send_frame(Stream &s,
                                  uint8_t msg_id,
                                  const uint8_t *payload,
                                  uint8_t len,
                                  uint8_t crc_extra) {
    static uint8_t seq = 0;
    uint8_t header[6] = {
        MAVLINK_STX_V1,
        len,
        seq++,
        MAV_SYSTEM_ID,
        MAV_COMPONENT_ID,
        msg_id
    };

    uint16_t crc;
    mav_crc_init(&crc);
    // CRC sobre [LEN, SEQ, SYS, COMP, MSG]
    for (uint8_t i = 1; i < 6; i++) mav_crc_accumulate(header[i], &crc);
    // CRC sobre PAYLOAD
    for (uint8_t i = 0; i < len; i++) mav_crc_accumulate(payload[i], &crc);
    // CRC_EXTRA (seed por message id, segredo da spec MAVLink)
    mav_crc_accumulate(crc_extra, &crc);

    // Envia: header + payload + CRC (little-endian)
    s.write(header, 6);
    if (len > 0) s.write(payload, len);
    s.write((uint8_t)(crc & 0xFF));
    s.write((uint8_t)(crc >> 8));
}

// =============================================================================
// HEARTBEAT (MSG ID 0) — 9 bytes payload
//
// Layout (little-endian):
//   uint32  custom_mode           (modo customizado, deixamos 0)
//   uint8   type                  (MAV_TYPE_FIXED_WING = 1)
//   uint8   autopilot             (MAV_AUTOPILOT_GENERIC = 0)
//   uint8   base_mode             (MAV_MODE_FLAG_*)
//   uint8   system_status         (MAV_STATE_*)
//   uint8   mavlink_version       (3 desde MAVLink v0.9)
// =============================================================================
static inline void mav_send_heartbeat(Stream &s,
                                      uint8_t base_mode = 0,
                                      uint8_t system_status = MAV_STATE_ACTIVE) {
    uint8_t payload[MAVLINK_PAYLOAD_LEN_HEARTBEAT] = {0};

    // custom_mode (uint32, offset 0-3) — deixamos 0
    payload[0] = 0; payload[1] = 0; payload[2] = 0; payload[3] = 0;
    // type (uint8, offset 4)
    payload[4] = MAV_TYPE_FIXED_WING;
    // autopilot (uint8, offset 5)
    payload[5] = MAV_AUTOPILOT_GENERIC;
    // base_mode (uint8, offset 6)
    payload[6] = base_mode;
    // system_status (uint8, offset 7)
    payload[7] = system_status;
    // mavlink_version (uint8, offset 8)
    payload[8] = 3;

    mav_send_frame(s, MAVLINK_MSG_ID_HEARTBEAT, payload,
                   MAVLINK_PAYLOAD_LEN_HEARTBEAT,
                   MAVLINK_CRC_HEARTBEAT);
}

// =============================================================================
// RC_CHANNELS_RAW (MSG ID 35) — 22 bytes payload
//
// Layout (little-endian):
//   uint32  time_boot_ms          (ms desde boot)
//   uint16  chan1_raw ... chan8_raw  (8 canais em µs, 1000-2000)
//   uint8   port                  (0 = primário)
//   uint8   rssi                  (0-255, ou UINT8_MAX se indisponível)
// =============================================================================
static inline void mav_send_rc_channels_raw(Stream &s,
                                            uint32_t time_boot_ms,
                                            const uint16_t channels_us[8],
                                            uint8_t rssi = 255) {
    uint8_t payload[MAVLINK_PAYLOAD_LEN_RC_CHANNELS_RAW] = {0};

    // time_boot_ms (offset 0-3)
    payload[0] = (uint8_t)(time_boot_ms        & 0xFF);
    payload[1] = (uint8_t)((time_boot_ms >> 8) & 0xFF);
    payload[2] = (uint8_t)((time_boot_ms >> 16) & 0xFF);
    payload[3] = (uint8_t)((time_boot_ms >> 24) & 0xFF);

    // chan1..chan8 (offset 4-19) — cada um uint16 LE
    for (uint8_t i = 0; i < 8; i++) {
        payload[4 + i * 2]     = (uint8_t)(channels_us[i] & 0xFF);
        payload[4 + i * 2 + 1] = (uint8_t)(channels_us[i] >> 8);
    }

    // port (offset 20)
    payload[20] = 0;
    // rssi (offset 21)
    payload[21] = rssi;

    mav_send_frame(s, MAVLINK_MSG_ID_RC_CHANNELS_RAW, payload,
                   MAVLINK_PAYLOAD_LEN_RC_CHANNELS_RAW,
                   MAVLINK_CRC_RC_CHANNELS_RAW);
}

#endif // MAVLINK_MIN_H
