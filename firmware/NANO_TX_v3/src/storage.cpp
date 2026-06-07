#include <EEPROM.h>
#include "storage.h"

// -----------------------------------------------------------------------------
// Estado global do módulo
// -----------------------------------------------------------------------------
PersistData persist;

// Flag e timestamp do commit diferido dos trims
static bool     s_trims_dirty       = false;
static uint32_t s_last_dirty_ms     = 0;

// Endereço do bloco PersistData (depois do magic + version)
static constexpr int EE_DATA_ADDR = EE_BASE_ADDR + 2;

// -----------------------------------------------------------------------------
// Defaults (quando a EEPROM tá virgem ou layout incompatível)
// -----------------------------------------------------------------------------
static void fillDefaults(PersistData& d) {
    for (uint8_t i = 0; i < NUM_GIMBALS; i++) {
        d.calib.minRaw[i]    = 0;
        d.calib.maxRaw[i]    = 1023;
        d.calib.centerRaw[i] = 512;
        d.trimOffset[i]      = 0;
    }
}

// -----------------------------------------------------------------------------
// Header check
// -----------------------------------------------------------------------------
static bool headerValid() {
    return EEPROM.read(EE_BASE_ADDR + 0) == EE_MAGIC
        && EEPROM.read(EE_BASE_ADDR + 1) == EE_VERSION;
}

static void writeHeader() {
    EEPROM.update(EE_BASE_ADDR + 0, EE_MAGIC);
    EEPROM.update(EE_BASE_ADDR + 1, EE_VERSION);
}

// -----------------------------------------------------------------------------
// API
// -----------------------------------------------------------------------------
void storage_setup() {
    if (headerValid()) {
        EEPROM.get(EE_DATA_ADDR, persist);
        // Sanidade extra: se algum trimOffset vier fora da faixa (ex.: corrompido
        // por raio cósmico ou rebaixamento de TRIM_MAX_ABS entre versões),
        // clampa em vez de confiar cegamente.
        for (uint8_t i = 0; i < NUM_GIMBALS; i++) {
            if (persist.trimOffset[i] >  TRIM_MAX_ABS) persist.trimOffset[i] =  TRIM_MAX_ABS;
            if (persist.trimOffset[i] < -TRIM_MAX_ABS) persist.trimOffset[i] = -TRIM_MAX_ABS;
        }
    } else {
        // EEPROM virgem ou layout antigo → escreve defaults limpos
        fillDefaults(persist);
        writeHeader();
        EEPROM.put(EE_DATA_ADDR, persist);   // put() usa update internamente
    }
}

void storage_mark_trims_dirty() {
    s_trims_dirty   = true;
    s_last_dirty_ms = millis();
}

void storage_save_calib_now() {
    // Grava o bloco INTEIRO (calib + trims juntos). put() ignora bytes iguais,
    // então só o que mudou de fato vai virar write.
    writeHeader();
    EEPROM.put(EE_DATA_ADDR, persist);
    // Calib salva também limpa o pending de trims (já gravou tudo)
    s_trims_dirty = false;
}

void storage_tick(uint32_t now_ms) {
    if (!s_trims_dirty) return;
    if ((now_ms - s_last_dirty_ms) < EE_COMMIT_QUIET_MS) return;

    // Estabilizou: faz o commit. put() já é update-style, então só grava
    // os bytes do trimOffset[] que mudaram desde a última leitura.
    writeHeader();
    EEPROM.put(EE_DATA_ADDR, persist);
    s_trims_dirty = false;
}
