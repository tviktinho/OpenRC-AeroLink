#pragma once

#include <stdint.h>

namespace MavlinkRxTask {
    void start();

    // Processa um byte recebido (de UART2 ou Wi-Fi).
    // Usa MAVLINK_COMM_1 (UART2) ou MAVLINK_COMM_2 (Wi-Fi) pra evitar race no parser.
    void processByte(uint8_t c, uint8_t channel);
}
