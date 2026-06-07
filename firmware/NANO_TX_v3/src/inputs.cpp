#include <Wire.h>
#include "inputs.h"
#include "storage.h"

// -----------------------------------------------------------------------------
// Estado público
// -----------------------------------------------------------------------------
uint8_t potByte[NUM_GIMBALS] = { 128, 128, 128, 128 };
uint8_t sw1b = 0;
uint8_t sw2b = 0;
uint8_t auxb = 0;
bool    calibrating = false;
bool    pcf_ok      = false;
uint8_t pcf_raw     = 0xFF;     // 0xFF = todos soltos (estado inicial)

// -----------------------------------------------------------------------------
// Pinos de gimbal num array (mesmo padrão do v2, mas só 4)
// -----------------------------------------------------------------------------
static const uint8_t GIMBAL_PIN[NUM_GIMBALS] = {
    PIN_GIMBAL_0, PIN_GIMBAL_1, PIN_GIMBAL_2, PIN_GIMBAL_3
};

// -----------------------------------------------------------------------------
// Helpers de calibração (copiados do v2, com NUM_GIMBALS=4 em vez de 8)
// -----------------------------------------------------------------------------
static void resetCalibrationBounds() {
    for (uint8_t i = 0; i < NUM_GIMBALS; i++) {
        persist.calib.minRaw[i]    = 1023;
        persist.calib.maxRaw[i]    = 0;
        // centerRaw fica como tá; será reescrito no finalize
    }
}

static void finalizeCalibrationBounds() {
    // Capta o centro no momento que o botão é solto. Premissa: usuário deixou
    // os sticks no centro físico antes de soltar (mesmo gesto do v2).
    for (uint8_t i = 0; i < NUM_GIMBALS; i++) {
        // Eixo não-mexido (range minúsculo) → volta pra faixa cheia segura
        if (persist.calib.maxRaw[i] <= persist.calib.minRaw[i]
            || (persist.calib.maxRaw[i] - persist.calib.minRaw[i]) < 8) {
            persist.calib.minRaw[i]    = 0;
            persist.calib.maxRaw[i]    = 1023;
            persist.calib.centerRaw[i] = 512;
            continue;
        }
        uint16_t ctr = (uint16_t)analogRead(GIMBAL_PIN[i]);
        // Sanidade: centro precisa caber dentro de [min, max]
        if (ctr < persist.calib.minRaw[i]) ctr = persist.calib.minRaw[i] + 1;
        if (ctr > persist.calib.maxRaw[i]) ctr = persist.calib.maxRaw[i] - 1;
        persist.calib.centerRaw[i] = ctr;
    }
}

static void updateCalibrationBoundsOnce() {
    for (uint8_t i = 0; i < NUM_GIMBALS; i++) {
        int r = analogRead(GIMBAL_PIN[i]);
        if ((uint16_t)r < persist.calib.minRaw[i]) persist.calib.minRaw[i] = (uint16_t)r;
        if ((uint16_t)r > persist.calib.maxRaw[i]) persist.calib.maxRaw[i] = (uint16_t)r;
    }
}

// -----------------------------------------------------------------------------
// Mapeamento ADC → byte (2 segmentos com centro físico fixado no byte 128)
// IDÊNTICO ao v2: [mn..ce] → [0..128] e [ce..mx] → [128..255].
// Durante a calibração usamos a versão linear (faixa cheia 0..1023).
// -----------------------------------------------------------------------------
static inline uint8_t normalizeToByteCentered(int raw, uint16_t mn, uint16_t ce, uint16_t mx) {
    if (mx <= mn) { mn = 0; mx = 1023; ce = 512; }
    if (raw < (int)mn) raw = mn;
    if (raw > (int)mx) raw = mx;
    if (raw <= (int)ce) {
        return (uint8_t)map(raw, mn, ce, 0, 128);
    } else {
        return (uint8_t)map(raw, ce, mx, 128, 255);
    }
}

static inline uint8_t normalizeToByteLinear(int raw, uint16_t mn, uint16_t mx) {
    if (mx <= mn) { mn = 0; mx = 1023; }
    if (raw < (int)mn) raw = mn;
    if (raw > (int)mx) raw = mx;
    return (uint8_t)map(raw, mn, mx, 0, 255);
}

// -----------------------------------------------------------------------------
// Aplica trim no byte calibrado. Saturação 0..255 (constrain).
// O trim_offset é mantido pelo módulo trims.cpp em persist.trimOffset[i].
// -----------------------------------------------------------------------------
static inline uint8_t applyTrim(uint8_t calibratedByte, int8_t offset) {
    int16_t v = (int16_t)calibratedByte + (int16_t)offset;
    if (v < 0)   v = 0;
    if (v > 255) v = 255;
    return (uint8_t)v;
}

// -----------------------------------------------------------------------------
// PCF8574 — leitura via Wire direto
// -----------------------------------------------------------------------------
static bool pcfProbe(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

static void pcfWriteAllHigh(uint8_t addr) {
    // No PCF8574 com I/O quasi-bidirecional, escrever 0xFF "libera" todos os
    // pinos como entradas com pull-up fraco interno (~100 µA). É o estado em
    // que os botões pra GND vão puxar bit pra 0.
    Wire.beginTransmission(addr);
    Wire.write(0xFF);
    Wire.endTransmission();
}

static uint8_t pcfRead(uint8_t addr) {
    Wire.requestFrom(addr, (uint8_t)1);
    if (Wire.available()) return Wire.read();
    return 0xFF;   // nada veio → assume "tudo solto" (failsafe)
}

// -----------------------------------------------------------------------------
// Scanner I2C de boot. Só roda no setup() ANTES do CRSF começar — pode
// imprimir no Serial sem corromper o link CRSF.
// -----------------------------------------------------------------------------
static void scanI2CAndLog() {
    Serial.println(F("[I2C scan]"));
    uint8_t found = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.print(F("  device @ 0x"));
            if (addr < 0x10) Serial.print('0');
            Serial.println(addr, HEX);
            found++;
        }
    }
    if (!found) Serial.println(F("  (nenhum dispositivo respondeu)"));
}

// -----------------------------------------------------------------------------
// API
// -----------------------------------------------------------------------------
bool inputs_setup() {
    pinMode(PIN_SW1,   INPUT_PULLUP);
    pinMode(PIN_SW2,   INPUT_PULLUP);
    pinMode(PIN_CALIB, INPUT_PULLUP);
    pinMode(PIN_AUX,   INPUT_PULLUP);
    // analogRead nos pinos A0..A3 não precisa de pinMode.

    Wire.begin();
    Wire.setClock(I2C_CLOCK_HZ);

    // Scanner roda usando o Serial padrão (USB) APENAS porque o CRSF ainda
    // não começou. Quem chamar inputs_setup() DEVE ter chamado Serial.begin()
    // antes (no main.cpp), com um baud útil pra debug. Depois do scanner,
    // o main.cpp reconfigura Serial pro baud do CRSF.
    scanI2CAndLog();

    pcf_ok = pcfProbe(PCF_ADDR);
    if (pcf_ok) {
        Serial.print(F("[PCF8574] OK @ 0x"));
        Serial.println(PCF_ADDR, HEX);
        pcfWriteAllHigh(PCF_ADDR);   // deixa pronto pra leitura
    } else {
        Serial.print(F("[PCF8574] NAO RESPONDE @ 0x"));
        Serial.print(PCF_ADDR, HEX);
        Serial.println(F("  — trims digitais inativos."));
    }

    // Inicialização defensiva da leitura dos pots (sem trim, sem filtro)
    for (uint8_t i = 0; i < NUM_GIMBALS; i++) {
        int raw = analogRead(GIMBAL_PIN[i]);
        uint8_t nrm = normalizeToByteCentered(raw,
                                              persist.calib.minRaw[i],
                                              persist.calib.centerRaw[i],
                                              persist.calib.maxRaw[i]);
        potByte[i] = applyTrim(nrm, persist.trimOffset[i]);
    }

    return pcf_ok;
}

void inputs_tick() {
    // -------------------------------------------------------------------------
    // 1) Botão de calibração D6: edge detection do gesto press/release
    // -------------------------------------------------------------------------
    const bool calPressed = (digitalRead(PIN_CALIB) == LOW);
    static bool calPrev = false;

    if (calPressed && !calPrev) {
        // Início do gesto
        calibrating = true;
        resetCalibrationBounds();
    }
    if (!calPressed && calPrev) {
        // Fim do gesto — captura centro e SALVA na EEPROM (1 write/gesto)
        finalizeCalibrationBounds();
        calibrating = false;
        storage_save_calib_now();
    }
    calPrev = calPressed;

    if (calibrating) {
        updateCalibrationBoundsOnce();
    }

    // -------------------------------------------------------------------------
    // 2) Gimbals A0..A3 com filtro IIR (mesmo do v2)
    // -------------------------------------------------------------------------
    for (uint8_t i = 0; i < NUM_GIMBALS; i++) {
        int raw = analogRead(GIMBAL_PIN[i]);
        uint8_t nrm = calibrating
            ? normalizeToByteLinear(raw, 0, 1023)
            : normalizeToByteCentered(raw,
                                      persist.calib.minRaw[i],
                                      persist.calib.centerRaw[i],
                                      persist.calib.maxRaw[i]);

        // Filtro exponencial leve do v2.
        // (signed delta dividido por (SMOOTHING+1), depois somado)
        // OBS: o filtro acontece SOBRE o byte JÁ-COM-TRIM. Isso é importante
        // porque queremos suavizar a saída final, não o sinal bruto.
        uint8_t target = applyTrim(nrm, persist.trimOffset[i]);
        potByte[i] = (uint8_t)(potByte[i] + (int)(target - potByte[i]) / (GIMBAL_SMOOTHING + 1));
    }

    // -------------------------------------------------------------------------
    // 3) Switches SW1, SW2, AUX (lógica binária pura)
    // -------------------------------------------------------------------------
    sw1b = (digitalRead(PIN_SW1) == LOW) ? 255 : 0;
    sw2b = (digitalRead(PIN_SW2) == LOW) ? 255 : 0;
    auxb = (digitalRead(PIN_AUX) == LOW) ? 255 : 0;

    // -------------------------------------------------------------------------
    // 4) PCF8574 (se presente). Lê 1 byte bruto; o debounce e a aplicação
    //    como trim ficam em trims_tick().
    // -------------------------------------------------------------------------
    if (pcf_ok) {
        // Em I/O quasi-bidirecional do PCF, escrever 0xFF antes do read garante
        // que os pinos estejam em "input mode" (pull-up interno fraco). Algumas
        // libs fazem isso só no setup; eu prefiro reescrever a cada leitura
        // pra ser robusto contra glitch de alimentação que zere o latch.
        pcfWriteAllHigh(PCF_ADDR);
        pcf_raw = pcfRead(PCF_ADDR);
    } else {
        pcf_raw = 0xFF;   // "tudo solto" — nenhum trim será disparado
    }
}
