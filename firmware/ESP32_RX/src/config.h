#pragma once

#include <stdint.h>

// =============================================================================
// PINAGEM
// =============================================================================

// PWM saídas (mantém pinagem original)
constexpr uint8_t PIN_ESC      = 12;
constexpr uint8_t PIN_SERVO_L  = 13;
constexpr uint8_t PIN_SERVO_R  = 14;

// nRF24L01 (mantém pinagem original — não muda o que já funciona)
constexpr uint8_t PIN_NRF_CE   = 4;
constexpr uint8_t PIN_NRF_CSN  = 5;
// SPI: SCK=18, MISO=19, MOSI=23 (VSPI default)

// I2C (MPU6050 + barômetro futuro compartilham)
constexpr uint8_t PIN_I2C_SDA  = 21;
constexpr uint8_t PIN_I2C_SCL  = 22;

// UART1 — GPS
// GPIO35 é input-only no ESP32, perfeito pra RX. TX reservado mas geralmente não usado.
constexpr uint8_t PIN_GPS_RX   = 35;
constexpr uint8_t PIN_GPS_TX   = 33;
constexpr uint32_t GPS_BAUD    = 9600;

// UART2 — MAVLink (MinimOSD TX + SiK RX/TX via Y-split físico)
constexpr uint8_t PIN_MAV_TX   = 17;
constexpr uint8_t PIN_MAV_RX   = 16;
constexpr uint32_t MAV_BAUD    = 57600;

// =============================================================================
// IDs MAVLink
// =============================================================================
constexpr uint8_t MAV_SYSTEM_ID    = 1;
constexpr uint8_t MAV_COMPONENT_ID = 1;

// =============================================================================
// PARÂMETROS DE VOO (extraído do firmware original)
// =============================================================================
constexpr int PWM_MIN       = 1000;
constexpr int PWM_MAX       = 2000;
constexpr int PWM_IDLE      = 1000;
constexpr int LOOP_FREQ_HZ  = 333;
constexpr uint32_t RX_TIMEOUT_MS = 200;

constexpr float G_E         = 1.0f;
constexpr float G_A         = 1.0f;
constexpr float EXPO_E      = 0.5f;
constexpr float EXPO_A      = 0.5f;
constexpr float DIFF        = 0.50f;
constexpr int   REFLEX_US   = -30;
constexpr float MAX_RATE_DPS = 300.0f;
constexpr float MANUAL_GAIN  = 300.0f;

constexpr float AHRS_ALPHA = 0.98f;
constexpr float RAD2DEG    = 57.295779513082320876f;
constexpr float DEG2RAD    = 0.017453292519943295f;

// =============================================================================
// Orientação do IMU
// =============================================================================
// Convenção esperada: X=nariz, Y=asa direita, Z=baixo.
// Se o chip foi montado em outra orientação, ajusta aqui SEM mexer no imu_task.
// Workflow: tenta swap, depois ajusta sinais até roll e pitch responderem
// na direção certa no HUD do Mission Planner.
#define IMU_SWAP_XY    1   // 1 = troca X com Y (chip rotacionado 90°)
#define IMU_INVERT_X   1   // 1 = inverte sinal do eixo X (depois do swap)
#define IMU_INVERT_Y   1   // 1 = inverte sinal do eixo Y (depois do swap)
#define IMU_INVERT_Z   0   // 1 = inverte sinal do eixo Z (yaw)

// Canais do pacote RF
constexpr uint8_t CH_THR = 0;
constexpr uint8_t CH_ELE = 3;
constexpr uint8_t CH_AIL = 2;

// Reversão de canais (1 = inverte o sinal antes de virar comando)
#define REVERSE_AIL   1
#define REVERSE_ELE   0

// =============================================================================
// Wi-Fi bridge MAVLink
// =============================================================================
// 1 = ESP32 cria AP "OpenRC-AeroLink", aceita Mission Planner via TCP 5760.
// 0 = Wi-Fi desligado (economiza ~80 mA e CPU).
#define WIFI_BRIDGE_ENABLE  1

// Modo AP: ESP32 cria a rede própria (recomendado pra campo).
// Modo STA: ESP32 conecta em rede existente (bancada).
#define WIFI_USE_AP         1   // 1 = AP, 0 = STA

#define WIFI_SSID           "OpenRC-AeroLink"
#define WIFI_PASSWORD       "aerolink123"      // mín. 8 chars em WPA2
#define WIFI_TCP_PORT       5760               // padrão MAVLink TCP (ArduPilot SITL)

// =============================================================================
// DEBUG
// =============================================================================
// 1 = imprime no USB Serial (115200) cada frame MAVLink em hex.
// Deixe em 0 em voo real — overhead alto.
#define MAVLINK_DEBUG 0
