#pragma once

#include <Arduino.h>
#include "config.h"
#include "flight_data.h"
#include "wifi_bridge.h"

// MAVLink c_library_v2 — apenas headers. Clone em lib/mavlink/.
// Forçamos a saída em wire-protocol v1 (MinimOSD-Extra só fala v1).
// Os headers já tem `extern "C"` interno quando compilado como C++.
#include "common/mavlink.h"

namespace Mav {

inline HardwareSerial& port() { return Serial2; }

inline void begin() {
    port().begin(MAV_BAUD, SERIAL_8N1, PIN_MAV_RX, PIN_MAV_TX);
    mavlink_status_t* st = mavlink_get_channel_status(MAVLINK_COMM_0);
    st->flags |= MAVLINK_STATUS_FLAG_OUT_MAVLINK1;  // força v1 na saída
}

#if MAVLINK_DEBUG
inline void debugPrint(const uint8_t* buf, uint16_t len) {
    Serial.print("MAV[");
    Serial.print(len);
    Serial.print("] ");
    for (uint16_t i = 0; i < len; ++i) {
        if (buf[i] < 0x10) Serial.print('0');
        Serial.print(buf[i], HEX);
        Serial.print(' ');
    }
    Serial.println();
}
#endif

inline void sendMessage(const mavlink_message_t& msg) {
    uint8_t buf[MAVLINK_MAX_PACKET_LEN];
    uint16_t len = mavlink_msg_to_send_buffer(buf, &msg);
    port().write(buf, len);
    // Espelha pro cliente Wi-Fi (Mission Planner via TCP), se conectado.
    WifiBridge::write(buf, len);
#if MAVLINK_DEBUG
    debugPrint(buf, len);
#endif
}

// -----------------------------------------------------------------------------
// HEARTBEAT — 1 Hz
// Anuncia tipo do veículo, modo atual e estado de armamento. MinimOSD-Extra
// só começa a desenhar quando recebe heartbeats consecutivos com sysid esperado.
// -----------------------------------------------------------------------------
inline void sendHeartbeat() {
    FlightData::lock();
    bool armed       = g_fd.armed;
    bool manual_mode = g_fd.manual_mode;
    bool failsafe    = g_fd.failsafe_active;
    FlightData::unlock();

    uint8_t base_mode = MAV_MODE_FLAG_CUSTOM_MODE_ENABLED;
    if (armed)       base_mode |= MAV_MODE_FLAG_SAFETY_ARMED;
    if (manual_mode) base_mode |= MAV_MODE_FLAG_MANUAL_INPUT_ENABLED;
    else             base_mode |= MAV_MODE_FLAG_STABILIZE_ENABLED;

    uint32_t custom_mode = manual_mode ? 1 : 0;   // 0=STAB, 1=MANUAL
    uint8_t  system_status = failsafe ? MAV_STATE_CRITICAL
                          : armed    ? MAV_STATE_ACTIVE
                                     : MAV_STATE_STANDBY;

    mavlink_message_t msg;
    mavlink_msg_heartbeat_pack(
        MAV_SYSTEM_ID, MAV_COMPONENT_ID, &msg,
        MAV_TYPE_FIXED_WING,        // avião (mixing aileron/elevator)
        MAV_AUTOPILOT_GENERIC,
        base_mode,
        custom_mode,
        system_status
    );
    sendMessage(msg);
}

// -----------------------------------------------------------------------------
// ATTITUDE — 10 Hz
// Pitch/roll/yaw em RADIANOS, taxas em rad/s. É o que alimenta o horizonte
// virtual da OSD. Yaw aqui é integrado do giroscópio (deriva sem mag).
// -----------------------------------------------------------------------------
inline void sendAttitude() {
    FlightData::lock();
    float roll  = g_fd.roll_deg       * DEG2RAD;
    float pitch = g_fd.pitch_deg      * DEG2RAD;
    float yaw   = g_fd.yaw_deg        * DEG2RAD;
    float rr    = g_fd.roll_rate_dps  * DEG2RAD;
    float pr    = g_fd.pitch_rate_dps * DEG2RAD;
    float yr    = g_fd.yaw_rate_dps   * DEG2RAD;
    FlightData::unlock();

    mavlink_message_t msg;
    mavlink_msg_attitude_pack(
        MAV_SYSTEM_ID, MAV_COMPONENT_ID, &msg,
        millis(),
        roll, pitch, yaw,
        rr,   pr,    yr
    );
    sendMessage(msg);
}

// -----------------------------------------------------------------------------
// GPS_RAW_INT — 5 Hz
// Posição bruta direto do receptor. Lat/lon em graus*1e7, alt em mm.
// fix_type: 0=no fix, 2=2D, 3=3D. eph/epv = 65535 se desconhecido.
// -----------------------------------------------------------------------------
inline void sendGpsRawInt() {
    FlightData::lock();
    bool   fix   = g_fd.gps_fix;
    int32_t lat  = (int32_t)(g_fd.gps_lat * 1e7);
    int32_t lon  = (int32_t)(g_fd.gps_lon * 1e7);
    int32_t alt  = (int32_t)(g_fd.gps_alt_m * 1000.0f);
    uint16_t cog = (uint16_t)(g_fd.gps_cog_deg * 100.0f);
    uint16_t vel = (uint16_t)(g_fd.gps_speed_mps * 100.0f);   // cm/s
    uint8_t  sats = g_fd.gps_sats;
    FlightData::unlock();

    mavlink_message_t msg;
    mavlink_msg_gps_raw_int_pack(
        MAV_SYSTEM_ID, MAV_COMPONENT_ID, &msg,
        (uint64_t)millis() * 1000ULL,    // time_usec
        fix ? 3 : 0,
        lat, lon, alt,
        65535, 65535,                    // eph, epv: desconhecido
        vel,
        cog,
        sats,
        // --- campos extra do MAVLink v2 (passamos 0/inválido) ---
        0,                               // alt_ellipsoid (mm) — desconhecido
        0, 0, 0, 0,                      // h_acc, v_acc, vel_acc, hdg_acc (mm/cm/s)
        UINT16_MAX                       // yaw: inválido (sem mag absoluto)
    );
    sendMessage(msg);
}

// -----------------------------------------------------------------------------
// VFR_HUD — 5 Hz
// Painel "head-up" da OSD: ground speed, heading (graus 0-359), throttle %,
// altitude (preferimos baro, fallback GPS) e climb rate.
// -----------------------------------------------------------------------------
inline void sendVfrHud() {
    FlightData::lock();
    float gs      = g_fd.gps_speed_mps;
    int   hdg_raw = (int)g_fd.yaw_deg;
    int   thr_us  = g_fd.throttle_us;
    float alt     = (g_fd.baro_alt_m != 0.0f) ? g_fd.baro_alt_m : g_fd.gps_alt_m;
    FlightData::unlock();

    int16_t heading = ((hdg_raw % 360) + 360) % 360;
    int thr_pct = ((thr_us - PWM_MIN) * 100) / (PWM_MAX - PWM_MIN);
    if (thr_pct < 0)   thr_pct = 0;
    if (thr_pct > 100) thr_pct = 100;

    mavlink_message_t msg;
    mavlink_msg_vfr_hud_pack(
        MAV_SYSTEM_ID, MAV_COMPONENT_ID, &msg,
        0.0f,                       // airspeed: sem pitot
        gs,                         // groundspeed (m/s)
        heading,
        (uint16_t)thr_pct,
        alt,
        0.0f                        // climb rate: sem estimador ainda
    );
    sendMessage(msg);
}

// -----------------------------------------------------------------------------
// SYS_STATUS — 2 Hz
// Saúde do sistema. Bateria em mV/cA, percentual -1 = desconhecido.
// Sensors present/enabled/health zerados até termos sensores instrumentados.
// -----------------------------------------------------------------------------
inline void sendSysStatus() {
    FlightData::lock();
    uint16_t mv = (uint16_t)(g_fd.batt_voltage_v * 1000.0f);
    int16_t  ca = (int16_t)(g_fd.batt_current_a * 100.0f);
    int8_t   pct = g_fd.batt_remaining_pct;
    FlightData::unlock();

    mavlink_message_t msg;
    mavlink_msg_sys_status_pack(
        MAV_SYSTEM_ID, MAV_COMPONENT_ID, &msg,
        0, 0, 0,                    // sensors present / enabled / health
        500,                        // load (50.0%)
        mv,
        ca,
        pct,
        0, 0, 0, 0, 0, 0,           // drop_rate_comm, errors_comm, errors_count1..4
        // --- campos extra do MAVLink v2 ---
        0, 0, 0                     // sensors present_ext / enabled_ext / health_ext
    );
    sendMessage(msg);
}

// -----------------------------------------------------------------------------
// GLOBAL_POSITION_INT — 5 Hz
// Posição "fundida" (sem EKF real ainda: passamos o GPS direto, com relative_alt
// = baro). Quando houver fusão GPS+baro+IMU, esse é o canal pra mandar.
// -----------------------------------------------------------------------------
inline void sendGlobalPositionInt() {
    FlightData::lock();
    int32_t lat = (int32_t)(g_fd.gps_lat * 1e7);
    int32_t lon = (int32_t)(g_fd.gps_lon * 1e7);
    int32_t alt_mm = (int32_t)(g_fd.gps_alt_m * 1000.0f);
    int32_t rel_alt_mm = (int32_t)(g_fd.baro_alt_m * 1000.0f);
    int     yaw_raw = (int)g_fd.yaw_deg;
    FlightData::unlock();

    uint16_t hdg = (uint16_t)((((yaw_raw % 360) + 360) % 360) * 100);

    mavlink_message_t msg;
    mavlink_msg_global_position_int_pack(
        MAV_SYSTEM_ID, MAV_COMPONENT_ID, &msg,
        millis(),
        lat, lon, alt_mm,
        rel_alt_mm,
        0, 0, 0,                    // vx/vy/vz (cm/s): placeholder até ter fusão
        hdg
    );
    sendMessage(msg);
}

// -----------------------------------------------------------------------------
// Respostas a comandos do GCS (usadas pela mavlinkRxTask)
// -----------------------------------------------------------------------------
inline void sendCommandAck(uint16_t command, uint8_t result) {
    mavlink_message_t msg;
    mavlink_msg_command_ack_pack(
        MAV_SYSTEM_ID, MAV_COMPONENT_ID, &msg,
        command, result,
        // --- campos extra do MAVLink v2 ---
        0,        // progress
        0,        // result_param2
        0, 0      // target_system, target_component (broadcast)
    );
    sendMessage(msg);
}

inline void sendParamValue(const char* id, float value, uint16_t count, uint16_t index) {
    mavlink_message_t msg;
    mavlink_msg_param_value_pack(
        MAV_SYSTEM_ID, MAV_COMPONENT_ID, &msg,
        id, value, MAV_PARAM_TYPE_REAL32,
        count, index
    );
    sendMessage(msg);
}

inline void sendMissionAck(uint8_t target_sys, uint8_t target_comp, uint8_t type) {
    mavlink_message_t msg;
    mavlink_msg_mission_ack_pack(
        MAV_SYSTEM_ID, MAV_COMPONENT_ID, &msg,
        target_sys, target_comp, type,
        // --- campos extra do MAVLink v2 ---
        MAV_MISSION_TYPE_MISSION,   // mission_type
        0                           // opaque_id
    );
    sendMessage(msg);
}

}  // namespace Mav
