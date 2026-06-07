/* =============================================================================
 * storage.h — Persistência em EEPROM (calibração de gimbal + offsets de trim)
 *
 * Por que isso existe?
 *   - Calibração de gimbal: no v2 ficava em RAM (perdia no reset). Agora salva
 *     uma única vez por gesto (1 write/calibração → desgaste irrelevante).
 *   - Trims digitais: o usuário ajusta no campo; sem persistência ele tem que
 *     re-trimar a cada power-cycle. CHATO.
 *
 * Proteção contra desgaste da EEPROM (100k ciclos/célula no 328P):
 *   - EEPROM.put() internamente usa update(): só grava bytes que mudaram.
 *   - Commit DIFERIDO: storage_mark_trims_dirty() só seta uma flag. O
 *     storage_tick() faz o commit real depois de EE_COMMIT_QUIET_MS sem
 *     novas mudanças. Resultado: ~1 write por SESSÃO de ajuste,
 *     não por toque. Vida útil prática ≈ eterna.
 *
 * Layout da EEPROM (30 bytes a partir de EE_BASE_ADDR):
 *   +0   : MAGIC   (1 byte, 0xA5)
 *   +1   : VERSION (1 byte, 0x01)
 *   +2   : PersistData (28 bytes — ver struct abaixo)
 *
 * Se MAGIC ou VERSION não baterem, storage_load() inicializa valores default
 * e re-grava (escreve o header novo). Isso protege contra:
 *   - EEPROM virgem (chip novo): vem com 0xFF, magic não bate
 *   - Migração de layout (mudar struct): bump em EE_VERSION força reset
 * ============================================================================= */

#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include "config.h"

// -----------------------------------------------------------------------------
// Calibração do gimbal — 3 pontos por eixo (min, centro, max), igual ao v2.
// O centro é capturado quando o usuário solta o botão D6 (premissa: sticks
// no centro físico naquele instante).
// -----------------------------------------------------------------------------
struct CalibGimbal {
    uint16_t minRaw[NUM_GIMBALS];
    uint16_t maxRaw[NUM_GIMBALS];
    uint16_t centerRaw[NUM_GIMBALS];
};

// -----------------------------------------------------------------------------
// Bloco persistido. Total = 4*2 + 4*2 + 4*2 + 4 = 28 bytes.
// Mantém alinhado em bytes (struct é __packed?) — em AVR uint16 não exige
// alinhamento, então é seguro. EEPROM.put grava raw bytes mesmo.
// -----------------------------------------------------------------------------
struct PersistData {
    CalibGimbal calib;
    int8_t      trimOffset[NUM_GIMBALS];   // ±TRIM_MAX_ABS, mesmo índice dos pots
};

// -----------------------------------------------------------------------------
// Acesso global ao bloco persistido. Mexa SOMENTE através das funções abaixo,
// porque elas marcam dirty e disparam o commit diferido.
// -----------------------------------------------------------------------------
extern PersistData persist;

// Inicializa. Tenta carregar; se MAGIC/VERSION não baterem, escreve defaults.
void storage_setup();

// Marca o bloco "trim" como sujo. Não grava imediatamente; storage_tick()
// faz o commit depois de EE_COMMIT_QUIET_MS sem novas dirtagens.
void storage_mark_trims_dirty();

// Grava imediatamente o bloco de calibração de gimbal (chamado pelo módulo de
// inputs no exato momento em que o gesto de calibração termina — 1 write
// por gesto, sem diferimento).
void storage_save_calib_now();

// Deve ser chamado uma vez por loop. Faz o commit diferido se aplicável.
void storage_tick(uint32_t now_ms);

#endif // STORAGE_H
