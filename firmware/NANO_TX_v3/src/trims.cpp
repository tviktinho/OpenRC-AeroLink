#include "trims.h"
#include "config.h"
#include "inputs.h"   // pcf_raw
#include "storage.h"  // persist.trimOffset + storage_mark_trims_dirty()

// -----------------------------------------------------------------------------
// Mapa de cada bit do PCF8574 → (axis, direção).
//
// axis indexa potByte / persist.trimOffset (0..3 = A0..A3).
// Convenção de funções (cf. config.h e RX):
//   axis 0 = Throttle (A0)
//   axis 1 = Yaw      (A1)
//   axis 2 = Pitch    (A2)
//   axis 3 = Roll     (A3)
// -----------------------------------------------------------------------------
struct TrimAction {
    int8_t axis;        // 0..3 (ou -1 se bit sem ação)
    int8_t direction;   // +1 ou -1
};

// PROGMEM economiza 16 bytes de RAM. Pra 8 entradas é simbólico, mas é um bom
// reflexo num projeto AVR que tende a apertar a SRAM.
static const TrimAction TRIM_MAP[8] PROGMEM = {
    /* bit 0 = P0 = Thr+ */ { 0, +1 },
    /* bit 1 = P1 = Thr− */ { 0, -1 },
    /* bit 2 = P2 = Roll+ */ { 3, +1 },
    /* bit 3 = P3 = Roll− */ { 3, -1 },
    /* bit 4 = P4 = Pit+  */ { 2, +1 },
    /* bit 5 = P5 = Pit−  */ { 2, -1 },
    /* bit 6 = P6 = Yaw+  */ { 1, +1 },
    /* bit 7 = P7 = Yaw−  */ { 1, -1 },
};

static inline TrimAction loadAction(uint8_t bit) {
    TrimAction a;
    a.axis      = (int8_t)pgm_read_byte(&TRIM_MAP[bit].axis);
    a.direction = (int8_t)pgm_read_byte(&TRIM_MAP[bit].direction);
    return a;
}

// -----------------------------------------------------------------------------
// Estado do debounce e auto-repeat
// -----------------------------------------------------------------------------
static uint8_t  s_stable        = 0xFF;   // estado debounced (1 = solto, 0 = pressionado)
static uint8_t  s_pending       = 0xFF;   // candidato pra próxima estabilização
static uint8_t  s_pending_count = 0;

// press_started_ms[i] = 0 significa "não pressionado".
// Cuidado: se millis() retornar 0 logo no boot e o usuário tiver segurando,
// poderíamos ter falso "não pressionado". Na prática millis() vira 0 só uma
// vez (no boot) e logo passa pra 1+. Ainda assim, tratamos o ms==0 como 1
// pra ser defensivo.
static uint32_t press_started_ms[8] = { 0 };
static uint32_t last_action_ms[8]   = { 0 };

// -----------------------------------------------------------------------------
// Aplica uma "tecla" de trim: incrementa offset com saturação na faixa
// e marca dirty pra storage gravar mais tarde.
// -----------------------------------------------------------------------------
static void fireTrim(uint8_t bit) {
    TrimAction a = loadAction(bit);
    if (a.axis < 0 || a.axis >= NUM_GIMBALS) return;

    int16_t v = (int16_t)persist.trimOffset[a.axis] + (int16_t)(a.direction * TRIM_STEP);
    if (v >  TRIM_MAX_ABS) v =  TRIM_MAX_ABS;
    if (v < -TRIM_MAX_ABS) v = -TRIM_MAX_ABS;
    persist.trimOffset[a.axis] = (int8_t)v;

    storage_mark_trims_dirty();
}

// -----------------------------------------------------------------------------
// API
// -----------------------------------------------------------------------------
void trims_setup() {
    // Nada além de inicialização padrão. O estado "tudo solto" já é o default
    // dos arrays estáticos (zerados pelo C startup).
    s_stable        = 0xFF;
    s_pending       = 0xFF;
    s_pending_count = 0;
}

void trims_tick(uint32_t now_ms) {
    // Proteção contra millis() == 0 (ver comentário acima)
    if (now_ms == 0) now_ms = 1;

    // -------------------------------------------------------------------------
    // 1) Debounce: precisa de BUTTON_DEBOUNCE_SAMPLES leituras iguais
    // -------------------------------------------------------------------------
    if (pcf_raw == s_pending) {
        if (s_pending_count < BUTTON_DEBOUNCE_SAMPLES) {
            s_pending_count++;
        }
    } else {
        s_pending       = pcf_raw;
        s_pending_count = 1;
    }
    if (s_pending_count >= BUTTON_DEBOUNCE_SAMPLES) {
        // -------------------------------------------------------------------------
        // 2) Promove pending → stable e processa as transições por bit
        // -------------------------------------------------------------------------
        uint8_t old_stable = s_stable;
        s_stable = s_pending;

        for (uint8_t b = 0; b < 8; b++) {
            const uint8_t mask = (uint8_t)(1u << b);
            const bool was_pressed = (old_stable & mask) == 0;
            const bool is_pressed  = (s_stable   & mask) == 0;

            if (is_pressed && !was_pressed) {
                // Borda: 1 → 0 (pressionou agora)
                fireTrim(b);
                press_started_ms[b] = now_ms;
                last_action_ms[b]   = now_ms;
            } else if (!is_pressed && was_pressed) {
                // Soltou: zera os timers do auto-repeat
                press_started_ms[b] = 0;
                last_action_ms[b]   = 0;
            }
        }
    }

    // -------------------------------------------------------------------------
    // 3) Auto-repeat: pra cada bit ainda pressionado, dispara se já passou
    //    do HOLD_MS e do REPEAT_MS desde a última ação.
    //
    //    Esta etapa roda TODO ciclo, independentemente do debounce ter
    //    promovido um novo stable, pra que o repeat seja regular no tempo.
    // -------------------------------------------------------------------------
    for (uint8_t b = 0; b < 8; b++) {
        if (press_started_ms[b] == 0) continue;          // não pressionado
        if ((s_stable & (1u << b)) != 0) continue;        // segurança extra

        const uint32_t held    = now_ms - press_started_ms[b];
        const uint32_t since   = now_ms - last_action_ms[b];

        if (held >= TRIM_HOLD_MS && since >= TRIM_REPEAT_MS) {
            fireTrim(b);
            last_action_ms[b] = now_ms;
        }
    }
}
