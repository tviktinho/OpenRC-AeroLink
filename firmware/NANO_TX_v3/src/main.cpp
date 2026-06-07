/* =============================================================================
 * NANO_TX v3 — Transmissor OpenRC AeroLink
 * Plataforma: Arduino Nano (ATmega328P @ 16 MHz)
 *
 * Novidades vs v2 (firmware/TX_NANO_v2):
 *   • Trims digitais via PCF8574 (I2C @ 0x20) — 8 botões (4 eixos × 2 direções)
 *   • Botão AUX binário extra em D2 (INPUT_PULLUP) → vira o NOVO CH7
 *   • Calibração de gimbal e offsets de trim PERSISTIDOS em EEPROM
 *   • Código modular (config / storage / inputs / trims / radio / crsf)
 *
 * INALTERADO (compat com os RX do projeto):
 *   • nRF24L01: addr "00001", canal 76, 250 kbps, AutoAck OFF, payload 10 B, 50 Hz
 *   • Frame CRSF idêntico (16 canais, 400 000 baud)
 *   • Layout da PacketRF (p[0..3] = A0..A3 com trim; p[4]/p[5] = SW1/SW2;
 *                         p[6] = AUX no v3; p[7]/s1/s2 = neutro 128)
 *
 * Loop a 50 Hz: ver LOOP_INTERVAL_MS em config.h.
 *
 * ATENÇÃO PRA GRAVAR:
 *   D0/D1 (HW Serial) é o CRSF. Desconecte o fio do TX0 → módulo CRSF antes
 *   do upload, senão o avrdude não fala com o bootloader.
 * ============================================================================= */

#include <Arduino.h>
#include "config.h"
#include "storage.h"
#include "inputs.h"
#include "trims.h"
#include "radio.h"
#include "crsf.h"

// Baud usado APENAS no boot pra prints de debug do scanner I2C.
// É reconfigurado pra CRSF_BAUDRATE assim que o setup termina.
#define BOOT_DEBUG_BAUD 115200UL

static uint32_t s_last_loop_ms = 0;

void setup() {
    // -------------------------------------------------------------------------
    // 1) Serial em baud de debug PRA O SCANNER I2C IMPRIMIR
    //    Depois desse setup, Serial vira CRSF — nenhum print é permitido.
    // -------------------------------------------------------------------------
    Serial.begin(BOOT_DEBUG_BAUD);
    delay(100);   // dá tempo do PC abrir o monitor (se quiser ver o scanner)
    Serial.println();
    Serial.print(F("[boot] "));
    Serial.print(F(FW_NAME));
    Serial.print(' ');
    Serial.println(F(FW_VERSION));

    // -------------------------------------------------------------------------
    // 2) Carrega EEPROM (calib + trims). Se EEPROM virgem, grava defaults.
    // -------------------------------------------------------------------------
    storage_setup();
    Serial.println(F("[storage] OK"));

    // -------------------------------------------------------------------------
    // 3) Configura entradas (gimbals, switches, AUX, PCF8574, Wire, scanner)
    // -------------------------------------------------------------------------
    inputs_setup();
    trims_setup();

    // Garante que o que tá no buffer Serial saia ANTES de mudar pra CRSF
    Serial.println(F("[boot] iniciando CRSF + nRF24..."));
    Serial.flush();
    delay(20);

    // -------------------------------------------------------------------------
    // 4) HW Serial passa a ser CRSF (400 000 baud). Nada de Serial.print
    //    depois disso — vai bagunçar o frame.
    // -------------------------------------------------------------------------
    crsf_setup();

    // -------------------------------------------------------------------------
    // 5) nRF24 (caminho ativo). Se falhar, o loop ignora o write.
    // -------------------------------------------------------------------------
    radio_setup();

    s_last_loop_ms = millis();
}

void loop() {
    const uint32_t now = millis();

    // Loop a 50 Hz exato. millis() rola em ~49 dias; (now - last) lida com
    // wrap aritmeticamente, sem precisar de cuidado especial.
    if ((now - s_last_loop_ms) < LOOP_INTERVAL_MS) return;
    s_last_loop_ms = now;

    // -------------------------------------------------------------------------
    // Pipeline de entrada → trim → persistência
    // -------------------------------------------------------------------------
    inputs_tick();           // lê tudo, atualiza potByte/sw/aux/pcf_raw + calib
    trims_tick(now);          // processa botões do PCF, atualiza offsets
    storage_tick(now);        // commit diferido na EEPROM se houver dirty

    // -------------------------------------------------------------------------
    // Durante a calibração: NÃO transmite (igual ao v2). O RX entra em
    // failsafe automaticamente — proteção enquanto sticks vão aos extremos.
    // -------------------------------------------------------------------------
    if (calibrating) return;

    // -------------------------------------------------------------------------
    // Saída dupla, em paralelo
    // -------------------------------------------------------------------------
    radio_send_packet();      // nRF24 (caminho ativo, pros C3/ESP)
    crsf_send_frame();        // CRSF (compat ELRS/Heltec)
}
