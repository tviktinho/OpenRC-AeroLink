// =============================================================================
// OpenRC AeroLink — ESP32 Flight Controller
//
// FC + RX + MAVLink (MinimOSD + SiK)
//
// Layout das tasks por core:
//
//   Core 1 (realtime — controle e IMU):
//     ImuTask        prio 3   (333 Hz)
//     ControlTask    prio 5   (333 Hz, PID + servos)
//
//   Core 0 (comunicação):
//     RadioTask      prio 4   (~200 Hz, lê nRF24)
//     GpsTask        prio 2
//     BaroTask       prio 2   (20 Hz)
//     OsdTask        prio 2   (envia stream MAVLink)
//     MavlinkRxTask  prio 2   (parser comandos GCS)
//
// FlightData::mutex sincroniza acesso à struct global compartilhada.
// =============================================================================

#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "flight_data.h"
#include "mavlink_helper.h"

#include "tasks/imu_task.h"
#include "tasks/radio_task.h"
#include "tasks/control_task.h"
#include "tasks/gps_task.h"
#include "tasks/baro_task.h"
#include "tasks/osd_task.h"
#include "tasks/mavlink_rx_task.h"
#include "tasks/wifi_task.h"

FlightData g_fd;
SemaphoreHandle_t FlightData::mutex = nullptr;

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("=== OpenRC AeroLink FC ===");
    Serial.println("Boot...");

    // I2C compartilhado: MPU6050 (imuTask) + barômetro futuro (baroTask)
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    // Scan I2C de boot (debug)
    Serial.println("I2C scan...");
    int found = 0;
    for (uint8_t a = 1; a < 127; ++a) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            Serial.print("  found 0x");
            Serial.println(a, HEX);
            ++found;
        }
    }
    Serial.print("I2C devices: ");
    Serial.println(found);

    FlightData::init();
    Mav::begin();  // Serial2 + força wire-protocol v1 antes das tasks rodarem

    ImuTask::start();
    RadioTask::start();
    ControlTask::start();
    GpsTask::start();
    BaroTask::start();
    OsdTask::start();
    MavlinkRxTask::start();
    WifiTask::start();

    Serial.println("Tasks criadas. Sistema pronto.");
}

void loop() {
    // Toda a lógica vive nas tasks. O loop() do Arduino vira efetivamente um
    // idle task de baixa prioridade no Core 1.
    vTaskDelay(pdMS_TO_TICKS(1000));
}
