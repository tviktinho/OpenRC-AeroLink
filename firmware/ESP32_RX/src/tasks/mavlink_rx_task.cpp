#include "mavlink_rx_task.h"
#include "imu_task.h"
#include "../config.h"
#include "../flight_data.h"
#include "../mavlink_helper.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ============================================================================
// mavlinkRxTask — lê comandos do GCS pela UART2 RX (SiK).
//
// Usa MAVLINK_COMM_1 pra parsing, separado do canal 0 que o osdTask usa pra
// TX — evita race condition no status do canal.
//
// Comandos implementados:
//   COMMAND_LONG MAV_CMD_COMPONENT_ARM_DISARM   → arm/disarm
//   COMMAND_LONG MAV_CMD_PREFLIGHT_CALIBRATION  → calibra gyro
//   PARAM_REQUEST_LIST / PARAM_REQUEST_READ     → envia tabela de params
//   PARAM_SET                                    → ecoa (não aplica ainda)
//   MISSION_REQUEST_LIST                         → responde count=0
//   MISSION_ITEM / MISSION_ITEM_INT              → ACK placeholder
// ============================================================================

namespace MavlinkRxTask {
namespace {

// Tabela mínima de parâmetros — extensível.
// TODO: ligar PARAM_SET ao control_task/radio_task pra realmente aplicar.
struct ParamEntry {
    char name[17];
    float value;
};

ParamEntry s_params[] = {
    {"KP_ROLL",  1.5f},
    {"KI_ROLL",  0.1f},
    {"KD_ROLL",  0.05f},
    {"KP_PITCH", 1.5f},
    {"KI_PITCH", 0.1f},
    {"KD_PITCH", 0.05f},
    {"DEADZONE", 0.05f},
};
constexpr uint16_t s_param_count = sizeof(s_params) / sizeof(s_params[0]);

void handleCommandLong(const mavlink_message_t& msg) {
    mavlink_command_long_t cmd;
    mavlink_msg_command_long_decode(&msg, &cmd);

    switch (cmd.command) {
        case MAV_CMD_COMPONENT_ARM_DISARM: {
            bool arm = cmd.param1 > 0.5f;
            FlightData::lock();
            g_fd.armed = arm;
            if (arm) g_fd.gcs_arm_request = true;
            else     g_fd.gcs_disarm_request = true;
            FlightData::unlock();
            Mav::sendCommandAck(cmd.command, MAV_RESULT_ACCEPTED);
            break;
        }
        case MAV_CMD_PREFLIGHT_CALIBRATION: {
            // param1=1 → gyro
            if (cmd.param1 > 0.5f) {
                ImuTask::requestGyroCalibration();
                Mav::sendCommandAck(cmd.command, MAV_RESULT_ACCEPTED);
            } else {
                Mav::sendCommandAck(cmd.command, MAV_RESULT_UNSUPPORTED);
            }
            break;
        }
        default:
            Mav::sendCommandAck(cmd.command, MAV_RESULT_UNSUPPORTED);
            break;
    }
}

void sendParamByIndex(uint16_t i) {
    if (i >= s_param_count) return;
    Mav::sendParamValue(s_params[i].name, s_params[i].value, s_param_count, i);
}

void handleParamRequestList() {
    // Espalha pra não saturar buffer de TX
    for (uint16_t i = 0; i < s_param_count; ++i) {
        sendParamByIndex(i);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void handleParamRequestRead(const mavlink_message_t& msg) {
    mavlink_param_request_read_t req;
    mavlink_msg_param_request_read_decode(&msg, &req);
    if (req.param_index >= 0 && req.param_index < (int)s_param_count) {
        sendParamByIndex((uint16_t)req.param_index);
        return;
    }
    for (uint16_t i = 0; i < s_param_count; ++i) {
        if (strncmp(s_params[i].name, req.param_id, 16) == 0) {
            sendParamByIndex(i);
            return;
        }
    }
}

void handleParamSet(const mavlink_message_t& msg) {
    mavlink_param_set_t set;
    mavlink_msg_param_set_decode(&msg, &set);
    for (uint16_t i = 0; i < s_param_count; ++i) {
        if (strncmp(s_params[i].name, set.param_id, 16) == 0) {
            s_params[i].value = set.param_value;
            sendParamByIndex(i);  // ecoa o valor aplicado
            return;
        }
    }
}

void handleMissionRequestList(const mavlink_message_t& msg) {
    mavlink_mission_request_list_t req;
    mavlink_msg_mission_request_list_decode(&msg, &req);
    mavlink_message_t out;
    mavlink_msg_mission_count_pack(
        MAV_SYSTEM_ID, MAV_COMPONENT_ID, &out,
        req.target_system, req.target_component,
        0,                              // 0 waypoints por enquanto
        MAV_MISSION_TYPE_MISSION,
        0                               // opaque_id (extra do MAVLink v2)
    );
    Mav::sendMessage(out);
}

void handleMissionItem(const mavlink_message_t& msg) {
    // Placeholder: aceita mas não armazena.
    uint8_t tgt_sys = 0, tgt_comp = 0;
    if (msg.msgid == MAVLINK_MSG_ID_MISSION_ITEM) {
        mavlink_mission_item_t item;
        mavlink_msg_mission_item_decode(&msg, &item);
        tgt_sys = item.target_system;
        tgt_comp = item.target_component;
    } else {
        mavlink_mission_item_int_t item;
        mavlink_msg_mission_item_int_decode(&msg, &item);
        tgt_sys = item.target_system;
        tgt_comp = item.target_component;
    }
    Mav::sendMissionAck(tgt_sys, tgt_comp, MAV_MISSION_ACCEPTED);
}

void dispatch(const mavlink_message_t& msg) {
    switch (msg.msgid) {
        case MAVLINK_MSG_ID_COMMAND_LONG:        handleCommandLong(msg);        break;
        case MAVLINK_MSG_ID_PARAM_REQUEST_LIST:  handleParamRequestList();      break;
        case MAVLINK_MSG_ID_PARAM_REQUEST_READ:  handleParamRequestRead(msg);   break;
        case MAVLINK_MSG_ID_PARAM_SET:           handleParamSet(msg);           break;
        case MAVLINK_MSG_ID_MISSION_REQUEST_LIST:handleMissionRequestList(msg); break;
        case MAVLINK_MSG_ID_MISSION_ITEM:
        case MAVLINK_MSG_ID_MISSION_ITEM_INT:    handleMissionItem(msg);        break;
        default: break;
    }
}

void task(void*) {
    Serial.println("[MAV_RX] Listening on UART2 RX");
    for (;;) {
        while (Serial2.available()) {
            processByte((uint8_t)Serial2.read(), MAVLINK_COMM_1);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

}  // anon

void processByte(uint8_t c, uint8_t channel) {
    // Estados de parsing por canal são separados (MAVLINK_COMM_1=UART2, _2=Wi-Fi)
    static mavlink_message_t msg[MAVLINK_COMM_NUM_BUFFERS];
    static mavlink_status_t  st[MAVLINK_COMM_NUM_BUFFERS];
    if (channel >= MAVLINK_COMM_NUM_BUFFERS) return;
    if (mavlink_parse_char(channel, c, &msg[channel], &st[channel])) {
        dispatch(msg[channel]);
    }
}

void start() {
    xTaskCreatePinnedToCore(task, "MavRxTask", 4096, nullptr, 2, nullptr, 0);
}

}  // namespace MavlinkRxTask
