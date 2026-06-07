/* =============================================================================
 * config.h — Pinos, endereços, constantes do NANO_TX v3
 *
 * Todo o "hardware abstraction" do projeto vive aqui. Se você muda a placa
 * física, mexe SÓ neste arquivo (e no diagrama do README).
 * ============================================================================= */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// -----------------------------------------------------------------------------
// Versão do firmware (impressa em alguns lugares)
// -----------------------------------------------------------------------------
#define FW_NAME     "NANO_TX_v3"
#define FW_VERSION  "0.1.0"

// =============================================================================
// PINOS
// =============================================================================

// --- nRF24L01 (SPI HW) -------------------------------------------------------
#define PIN_NRF_CE        8
#define PIN_NRF_CSN       7
// MOSI=11, MISO=12, SCK=13 — fixos pelo SPI HW do 328P, não dá pra mudar.
// D10 (SS) precisa ficar como OUTPUT mesmo sem ser usado, senão o SPI do 328P
// vira slave — a lib RF24 cuida disso no initRadio().

// --- Gimbals (4 eixos analógicos, leitura 10-bit) ----------------------------
// Convenção do projeto (lado RX): A0=Throttle, A1=Yaw, A2=Pitch, A3=Roll.
// O firmware NÃO precisa saber "Throttle/Yaw/Pitch/Roll"; só envia A0..A3 em
// p[0..3]. Os trims indexam o offset pelo MESMO índice de hardware (0..3).
#define PIN_GIMBAL_0      A0    // Throttle
#define PIN_GIMBAL_1      A1    // Yaw
#define PIN_GIMBAL_2      A2    // Pitch
#define PIN_GIMBAL_3      A3    // Roll
#define NUM_GIMBALS       4

// --- Switches físicos (do v2, mantidos) --------------------------------------
#define PIN_SW1           5     // D5, INPUT_PULLUP, ativo em LOW
#define PIN_SW2           4     // D4, INPUT_PULLUP, ativo em LOW

// --- Botão de calibração de gimbal (do v2, mantido) --------------------------
#define PIN_CALIB         6     // D6, INPUT_PULLUP, ativo em LOW

// --- Botão AUX binário NOVO --------------------------------------------------
// D2 escolhido por ser livre, INT0-capable (reserva pra futuro) e separado dos
// pinos SPI. Convenção INPUT_PULLUP, ativo em LOW (mesma dos SW1/SW2/CALIB).
#define PIN_AUX           2

// =============================================================================
// I2C / PCF8574 (placa de expansão dos trims digitais)
// =============================================================================
// SDA=A4, SCL=A5 — fixos pelo TWI HW do 328P.
//
// Endereço do PCF8574 com A0..A2 todos em GND:
//   - PCF8574  (sem "A") → 0x20
//   - PCF8574A (com "A") → 0x38
// Se o seu chip for o A, mude PCF_ADDR para 0x38. O scanner I2C no setup()
// loga via Serial qual endereço responde, antes do CRSF começar.
#define PCF_ADDR          0x20
#define I2C_CLOCK_HZ      100000UL    // 100 kHz é seguro; 400 kHz funciona pra cabos curtos

// Mapa dos 8 botões de trim no PCF8574 (bit 0 = pressionado):
//   P0=Thr+   P1=Thr−   P2=Roll+  P3=Roll−
//   P4=Pit+   P5=Pit−   P6=Yaw+   P7=Yaw−
//
// Cada botão é (axis_index, direction). Como o offset é indexado pelo
// índice físico do gimbal (0..3 = A0..A3), Throttle=0, Yaw=1, Pitch=2, Roll=3.
#define PCF_BIT_THR_INC   0
#define PCF_BIT_THR_DEC   1
#define PCF_BIT_ROL_INC   2
#define PCF_BIT_ROL_DEC   3
#define PCF_BIT_PIT_INC   4
#define PCF_BIT_PIT_DEC   5
#define PCF_BIT_YAW_INC   6
#define PCF_BIT_YAW_DEC   7

// =============================================================================
// RÁDIO nRF24 (idêntico ao v2)
// =============================================================================
#define RF_CHANNEL        76
extern const uint8_t RF_ADDRESS[6];   // definido em radio.cpp como "00001"

// =============================================================================
// TIMING / SUAVIZAÇÃO
// =============================================================================
#define LOOP_INTERVAL_MS         20       // 50 Hz
#define GIMBAL_SMOOTHING         2        // mesmo filtro do v2 (filtro IIR leve)

// Debounce dos botões do PCF e do AUX: o loop a 50 Hz já é a taxa de
// amostragem; pedimos N amostras consecutivas estáveis pra confirmar a
// transição. 2 amostras ≈ 40 ms de janela — robusto sem ficar lento.
#define BUTTON_DEBOUNCE_SAMPLES  2

// Auto-repeat dos trims (conforme decisão travada):
//   - Após segurar por HOLD_MS, começa a repetir
//   - Período entre repetições = REPEAT_MS (4 Hz = 250 ms)
#define TRIM_HOLD_MS             600
#define TRIM_REPEAT_MS           250

// =============================================================================
// TRIM (decisão travada: conservador)
// =============================================================================
// Passo de cada toque (em unidades de saída pós-map, 0..255).
#define TRIM_STEP                1

// Faixa máxima absoluta do offset. ±15 sobre uma escala 0..255 = ~6% do range,
// que é uma "trim leve" típica de rádio comercial.
#define TRIM_MAX_ABS             15

// =============================================================================
// EEPROM
// =============================================================================
// Layout completo está em storage.h. Aqui só o endereço-base e a assinatura.
// Magic distingue "EEPROM virgem / firmware errado" de "dados do v3".
#define EE_BASE_ADDR             0
#define EE_MAGIC                 0xA5
#define EE_VERSION               0x01

// Tempo de "quiet" antes de gravar trims na EEPROM. Evita 1 write por toque
// quando o usuário tá segurando o botão com auto-repeat. 2 s é folga ampla:
// só grava quando o usuário PARA de mexer.
#define EE_COMMIT_QUIET_MS       2000UL

#endif // CONFIG_H
