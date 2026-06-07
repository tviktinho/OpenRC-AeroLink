/* =============================================================================
 * inputs.h — Camada de entrada do NANO_TX v3
 *
 * Responsabilidades:
 *   1. Gimbals A0..A3: leitura + calibração (mesma lógica do v2) + filtro IIR
 *      + aplicação do trim_offset[] já calculado pelo módulo trims.
 *   2. Switches SW1/SW2: leitura digital com pullup interno.
 *   3. Botão de calibração D6: detecta gesto (press/release), durante o press
 *      varre os pots pra atualizar min/max, no release captura centro e
 *      dispara storage_save_calib_now().
 *   4. Botão AUX D2: leitura digital + estado atual (debounce é trivial pra
 *      saída binária; mas reaproveitamos a infra do módulo trims).
 *   5. PCF8574 via Wire direto: 1 escrita de 0xFF + 1 leitura de 1 byte
 *      por ciclo. Retorna o byte cru (bit 0 = pressionado).
 *
 * O QUE NÃO ESTÁ AQUI:
 *   - Lógica de trim (debounce, edge, auto-repeat, offset) — vive em trims.cpp.
 *   - Construção do PacketRF / CRSF — vive em radio/crsf.
 * ============================================================================= */

#ifndef INPUTS_H
#define INPUTS_H

#include <Arduino.h>
#include "config.h"

// -----------------------------------------------------------------------------
// Saídas processadas (lidas pelo crsf/radio na hora de montar os pacotes)
// -----------------------------------------------------------------------------
extern uint8_t potByte[NUM_GIMBALS];   // 0..255, JÁ COM trim_offset aplicado
extern uint8_t sw1b;                    // 0 ou 255
extern uint8_t sw2b;                    // 0 ou 255
extern uint8_t auxb;                    // 0 ou 255

// Estado da calibração (true = em curso → não transmita!)
extern bool calibrating;

// -----------------------------------------------------------------------------
// Estado do PCF8574 — exposto pra que o módulo trims trabalhe em cima dele.
// -----------------------------------------------------------------------------
extern bool    pcf_ok;        // true se respondeu no Wire.beginTransmission
extern uint8_t pcf_raw;       // último byte lido (bit 0 = pressionado)

// -----------------------------------------------------------------------------
// API
// -----------------------------------------------------------------------------

// Configura pinos, inicia Wire, faz um scanner I2C de boot (logando no Serial
// ANTES do CRSF começar). Retorna true se o PCF respondeu no endereço esperado.
// Não é fatal se retornar false — o firmware segue rodando, só os trims ficam
// inertes.
bool inputs_setup();

// Lê tudo: gimbals, switches, calib button, AUX, PCF8574.
// Atualiza calibrating, potByte[] (com trim), sw1b/sw2b/auxb e pcf_raw.
void inputs_tick();

#endif // INPUTS_H
