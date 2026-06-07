/* =============================================================================
 * crsf.h — Saída CRSF (compat ELRS/Heltec)
 *
 * Mesmo mapeamento da PacketRF + CH8..CH16 em centro (não temos dados pra
 * preencher). Mantém o módulo Heltec/ELRS feliz pelo bind/handshake.
 *
 * Baud do CRSF: 400 000 (cf. crsf_tx.h — o 328P não gera 420k limpo).
 * ============================================================================= */

#ifndef CRSF_H
#define CRSF_H

#include <Arduino.h>

void crsf_setup();          // Serial.begin(CRSF_BAUDRATE)
void crsf_send_frame();     // monta os 16 canais e dispara Serial.write

#endif // CRSF_H
