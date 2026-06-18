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
#include <string.h>

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

// =============================================================================
// ATTITUDE (MSG ID 30) — 28 bytes — alimenta o horizonte artificial do MP
//
// Layout (little-endian):
//   uint32 time_boot_ms
//   float  roll, pitch, yaw                (radianos)
//   float  rollspeed, pitchspeed, yawspeed (rad/s)
// =============================================================================
#define MAVLINK_MSG_ID_ATTITUDE          30
#define MAVLINK_CRC_ATTITUDE             39
#define MAVLINK_PAYLOAD_LEN_ATTITUDE     28

static inline void mav_put_float(uint8_t *p, float v) {
    memcpy(p, &v, 4);   // ESP32 e MAVLink são ambos little-endian
}

static inline void mav_send_attitude(Stream &s, uint32_t time_boot_ms,
                                     float roll, float pitch, float yaw,
                                     float rollspeed, float pitchspeed, float yawspeed) {
    uint8_t payload[MAVLINK_PAYLOAD_LEN_ATTITUDE] = {0};
    payload[0] = (uint8_t)(time_boot_ms        & 0xFF);
    payload[1] = (uint8_t)((time_boot_ms >> 8)  & 0xFF);
    payload[2] = (uint8_t)((time_boot_ms >> 16) & 0xFF);
    payload[3] = (uint8_t)((time_boot_ms >> 24) & 0xFF);
    mav_put_float(&payload[4],  roll);
    mav_put_float(&payload[8],  pitch);
    mav_put_float(&payload[12], yaw);
    mav_put_float(&payload[16], rollspeed);
    mav_put_float(&payload[20], pitchspeed);
    mav_put_float(&payload[24], yawspeed);
    mav_send_frame(s, MAVLINK_MSG_ID_ATTITUDE, payload,
                   MAVLINK_PAYLOAD_LEN_ATTITUDE, MAVLINK_CRC_ATTITUDE);
}

// =============================================================================
// STATUSTEXT (MSG ID 253) — 51 bytes — texto na aba "Messages" do Mission Planner
//   uint8 severity ; char text[50]
// =============================================================================
#define MAVLINK_MSG_ID_STATUSTEXT        253
#define MAVLINK_CRC_STATUSTEXT           83
#define MAVLINK_PAYLOAD_LEN_STATUSTEXT   51

#define MAV_SEVERITY_CRITICAL            2
#define MAV_SEVERITY_INFO                6

static inline void mav_send_statustext(Stream &s, uint8_t severity, const char *text) {
    uint8_t payload[MAVLINK_PAYLOAD_LEN_STATUSTEXT] = {0};
    payload[0] = severity;
    for (uint8_t i = 0; i < 50 && text[i]; i++) payload[1 + i] = (uint8_t)text[i];
    mav_send_frame(s, MAVLINK_MSG_ID_STATUSTEXT, payload,
                   MAVLINK_PAYLOAD_LEN_STATUSTEXT, MAVLINK_CRC_STATUSTEXT);
}

// =============================================================================
// Helpers de empacotamento little-endian (inteiros)
// =============================================================================
static inline void mav_put_u16(uint8_t *p, uint16_t v) { p[0]=v&0xFF; p[1]=v>>8; }
static inline void mav_put_i16(uint8_t *p, int16_t  v) { mav_put_u16(p, (uint16_t)v); }
static inline void mav_put_u32(uint8_t *p, uint32_t v) { for(int i=0;i<4;i++) p[i]=(v>>(8*i))&0xFF; }
static inline void mav_put_i32(uint8_t *p, int32_t  v) { mav_put_u32(p, (uint32_t)v); }
static inline void mav_put_u64(uint8_t *p, uint64_t v) { for(int i=0;i<8;i++) p[i]=(v>>(8*i))&0xFF; }

// =============================================================================
// GPS_RAW_INT (MSG ID 24) — 30 bytes — status/satélites do GPS no MP
//   uint64 time_usec; int32 lat,lon,alt; uint16 eph,epv,vel,cog;
//   uint8 fix_type, satellites_visible
// =============================================================================
#define MAVLINK_MSG_ID_GPS_RAW_INT        24
#define MAVLINK_CRC_GPS_RAW_INT           24
#define MAVLINK_PAYLOAD_LEN_GPS_RAW_INT   30

static inline void mav_send_gps_raw_int(Stream &s, uint64_t time_usec, uint8_t fix_type,
                                        int32_t lat, int32_t lon, int32_t alt,
                                        uint16_t vel, uint16_t cog, uint8_t sats) {
    uint8_t p[MAVLINK_PAYLOAD_LEN_GPS_RAW_INT] = {0};
    mav_put_u64(&p[0],  time_usec);
    mav_put_i32(&p[8],  lat);
    mav_put_i32(&p[12], lon);
    mav_put_i32(&p[16], alt);
    mav_put_u16(&p[20], 0xFFFF);   // eph (desconhecido)
    mav_put_u16(&p[22], 0xFFFF);   // epv
    mav_put_u16(&p[24], vel);
    mav_put_u16(&p[26], cog);
    p[28] = fix_type;
    p[29] = sats;
    mav_send_frame(s, MAVLINK_MSG_ID_GPS_RAW_INT, p,
                   MAVLINK_PAYLOAD_LEN_GPS_RAW_INT, MAVLINK_CRC_GPS_RAW_INT);
}

// =============================================================================
// GLOBAL_POSITION_INT (MSG ID 33) — 28 bytes — posição no mapa do MP
//   uint32 time_boot_ms; int32 lat,lon,alt,relative_alt; int16 vx,vy,vz; uint16 hdg
// =============================================================================
#define MAVLINK_MSG_ID_GLOBAL_POSITION_INT      33
#define MAVLINK_CRC_GLOBAL_POSITION_INT         104
#define MAVLINK_PAYLOAD_LEN_GLOBAL_POSITION_INT 28

static inline void mav_send_global_position_int(Stream &s, uint32_t time_boot_ms,
                                                int32_t lat, int32_t lon, int32_t alt,
                                                int32_t rel_alt, int16_t vx, int16_t vy,
                                                int16_t vz, uint16_t hdg) {
    uint8_t p[MAVLINK_PAYLOAD_LEN_GLOBAL_POSITION_INT] = {0};
    mav_put_u32(&p[0],  time_boot_ms);
    mav_put_i32(&p[4],  lat);
    mav_put_i32(&p[8],  lon);
    mav_put_i32(&p[12], alt);
    mav_put_i32(&p[16], rel_alt);
    mav_put_i16(&p[20], vx);
    mav_put_i16(&p[22], vy);
    mav_put_i16(&p[24], vz);
    mav_put_u16(&p[26], hdg);
    mav_send_frame(s, MAVLINK_MSG_ID_GLOBAL_POSITION_INT, p,
                   MAVLINK_PAYLOAD_LEN_GLOBAL_POSITION_INT, MAVLINK_CRC_GLOBAL_POSITION_INT);
}

// =============================================================================
// PARAM protocol — config em tempo real pelo Mission Planner
//   IDs e CRC_EXTRA das mensagens recebidas do GCS + envio de PARAM_VALUE
// =============================================================================
#define MAVLINK_MSG_ID_PARAM_REQUEST_READ  20
#define MAVLINK_CRC_PARAM_REQUEST_READ     214
#define MAVLINK_MSG_ID_PARAM_REQUEST_LIST  21
#define MAVLINK_CRC_PARAM_REQUEST_LIST     159
#define MAVLINK_MSG_ID_PARAM_VALUE         22
#define MAVLINK_CRC_PARAM_VALUE            220
#define MAVLINK_MSG_ID_PARAM_SET           23
#define MAVLINK_CRC_PARAM_SET              168
#define MAV_PARAM_TYPE_REAL32              9

// PARAM_VALUE (id 22) — 25 bytes. Ordem de wire: float, u16, u16, char[16], u8.
static inline void mav_send_param_value(Stream &s, const char *id, float value,
                                        uint16_t count, uint16_t index) {
    uint8_t p[25] = {0};
    mav_put_float(&p[0], value);
    mav_put_u16(&p[4], count);
    mav_put_u16(&p[6], index);
    for (uint8_t i = 0; i < 16 && id[i]; i++) p[8 + i] = (uint8_t)id[i];
    p[24] = MAV_PARAM_TYPE_REAL32;
    mav_send_frame(s, MAVLINK_MSG_ID_PARAM_VALUE, p, 25, MAVLINK_CRC_PARAM_VALUE);
}

// Parser MAVLink v1 (RX) — monta frames recebidos do GCS (PARAM_REQUEST_*/SET).
class MavParser {
public:
    uint8_t msgid = 0, plen = 0, payload[255];
    // Retorna true quando um frame completo termina (msgid/payload/plen válidos).
    bool feed(uint8_t b) {
        switch (st_) {
            case 0: if (b == MAVLINK_STX_V1) st_ = 1; return false;     // STX
            case 1: plen_ = b; idx_ = 0; mav_crc_init(&crc_);
                    mav_crc_accumulate(b, &crc_); st_ = 2; return false; // LEN
            case 2: case 3: case 4:                                     // SEQ/SYS/COMP
                    mav_crc_accumulate(b, &crc_); st_++; return false;
            case 5: msgid = b; mav_crc_accumulate(b, &crc_);
                    st_ = (plen_ > 0) ? 6 : 7; return false;            // MSGID
            case 6: payload[idx_++] = b; mav_crc_accumulate(b, &crc_);
                    if (idx_ >= plen_) st_ = 7; return false;           // PAYLOAD
            case 7: crc_rx_ = b; st_ = 8; return false;                 // CRC low
            case 8: crc_rx_ |= ((uint16_t)b << 8); st_ = 0;
                    plen = plen_; return true;                          // CRC high -> pronto
        }
        st_ = 0; return false;
    }
    bool crcOk(uint8_t crc_extra) const {
        uint16_t c = crc_; mav_crc_accumulate(crc_extra, &c); return c == crc_rx_;
    }
private:
    uint8_t  st_ = 0, plen_ = 0, idx_ = 0;
    uint16_t crc_ = 0, crc_rx_ = 0;
};

// =============================================================================
// COMMAND_LONG (RX) / COMMAND_ACK (TX) — botões de calibração do Mission Planner
// =============================================================================
#define MAVLINK_MSG_ID_COMMAND_LONG        76
#define MAVLINK_CRC_COMMAND_LONG           152
#define MAVLINK_MSG_ID_COMMAND_ACK         77
#define MAVLINK_CRC_COMMAND_ACK            143
#define MAV_CMD_PREFLIGHT_CALIBRATION      241
#define MAV_RESULT_ACCEPTED                0
#define MAV_RESULT_UNSUPPORTED             3

// COMMAND_ACK (id 77) — 3 bytes: uint16 command, uint8 result
static inline void mav_send_command_ack(Stream &s, uint16_t cmd, uint8_t result) {
    uint8_t p[3];
    mav_put_u16(&p[0], cmd);
    p[2] = result;
    mav_send_frame(s, MAVLINK_MSG_ID_COMMAND_ACK, p, 3, MAVLINK_CRC_COMMAND_ACK);
}

#endif // MAVLINK_MIN_H
