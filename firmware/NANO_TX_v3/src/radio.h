/* =============================================================================
 * radio.h — Saída nRF24L01 (caminho ATIVO pros RX do projeto)
 *
 * Mantém configuração IDÊNTICA ao v2 (canal 76, 250 kbps, AutoAck OFF, payload
 * 10 bytes, endereço "00001"). Os receptores C3/ESP/Nano dependem disso —
 * NÃO MUDAR sem atualizar todos os RX juntos.
 *
 * Diferença vs v2: o p[6] agora carrega o AUX binário (era o pot4 do v2).
 * O receptor mais novo (C3_RX_SBUS) lê p[6] como CH7 = canal genérico 0..255,
 * então 0/255 já funciona como switch sem mudança.
 * ============================================================================= */

#ifndef RADIO_H
#define RADIO_H

#include <Arduino.h>
#include "config.h"

// -----------------------------------------------------------------------------
// Estrutura do pacote — NÃO mude o layout (compatibilidade com RX).
// __packed garante 0 padding em qualquer compilador (no AVR já é assim por
// padrão, mas é seguro deixar explícito).
// -----------------------------------------------------------------------------
struct __attribute__((packed)) PacketRF {
    uint8_t p[8];   // p[0..3] = A0..A3 (com trim); p[4]=SW1; p[5]=SW2; p[6]=AUX; p[7]=reservado
    uint8_t s1;     // reservado (= 128)
    uint8_t s2;     // reservado (= 128)
};

// -----------------------------------------------------------------------------
// API
// -----------------------------------------------------------------------------
bool radio_setup();              // configura o rádio, retorna true se OK
bool radio_is_ready();           // status do último radio_setup()
void radio_send_packet();        // monta + envia 1 pacote (usa potByte/sw/aux)

#endif // RADIO_H
