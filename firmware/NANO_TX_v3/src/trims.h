/* =============================================================================
 * trims.h — Lógica dos 8 botões de trim do PCF8574
 *
 * Pipeline por loop:
 *   1. Lê pcf_raw (já feito por inputs_tick).
 *   2. Debounce: exige BUTTON_DEBOUNCE_SAMPLES amostras consecutivas iguais
 *      antes de promover ao estado "stable".
 *   3. Edge detect (transição 1→0 no stable = botão pressionado):
 *      - Dispara a ação UMA vez.
 *      - Memoriza press_started_ms[i] = now.
 *   4. Auto-repeat: enquanto o bit fica 0 e (now − press_started) ≥
 *      TRIM_HOLD_MS, dispara a ação a cada TRIM_REPEAT_MS.
 *   5. Ação = persist.trimOffset[axis] ± TRIM_STEP, clampado em ±TRIM_MAX_ABS,
 *      seguido de storage_mark_trims_dirty() (commit diferido).
 *
 * Por que essa estrutura?
 *   - Loop a 50 Hz já é a taxa de amostragem; o debounce de 2 amostras
 *     dá ~40 ms de janela, suficiente pra glitches mecânicos.
 *   - Edge na transição STABLE evita "trigger triplo" em borda ruidosa.
 *   - Commit diferido protege a EEPROM: usuário segurando o trim por 3 s com
 *     auto-repeat gera 1 write em vez de 10+ writes.
 * ============================================================================= */

#ifndef TRIMS_H
#define TRIMS_H

#include <Arduino.h>

void trims_setup();
void trims_tick(uint32_t now_ms);

#endif // TRIMS_H
