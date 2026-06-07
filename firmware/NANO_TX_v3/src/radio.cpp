#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include "radio.h"
#include "inputs.h"   // potByte, sw1b, sw2b, auxb

// Endereço de pipe — IDÊNTICO ao v2 (todos os RX esperam isso).
const uint8_t RF_ADDRESS[6] = "00001";

static RF24      s_radio(PIN_NRF_CE, PIN_NRF_CSN);
static PacketRF  s_pkt;
static bool      s_ready = false;

bool radio_setup() {
    delay(50);   // dá tempo do PA+LNA estabilizar a alimentação
    if (!s_radio.begin()) {
        s_ready = false;
        return false;
    }

    // SS (D10) precisa ser OUTPUT mesmo sem ser usado — senão o SPI HW
    // do 328P entra em modo slave. (Mesmo cuidado do v2.)
    pinMode(10, OUTPUT);

    s_radio.setPALevel(RF24_PA_LOW);
    s_radio.setDataRate(RF24_250KBPS);
    s_radio.setChannel(RF_CHANNEL);
    s_radio.setAutoAck(false);
    s_radio.setRetries(0, 0);
    s_radio.setPayloadSize(sizeof(PacketRF));
    s_radio.openWritingPipe(RF_ADDRESS);
    s_radio.stopListening();

    s_ready = s_radio.isChipConnected();
    return s_ready;
}

bool radio_is_ready() {
    return s_ready;
}

void radio_send_packet() {
    if (!s_ready) return;

    // Montagem na ordem CH1..CH10 (mesma do v2; pkts.p[6] = AUX no v3)
    s_pkt.p[0] = potByte[0];   // CH1  = A0 + trim
    s_pkt.p[1] = potByte[1];   // CH2  = A1 + trim
    s_pkt.p[2] = potByte[2];   // CH3  = A2 + trim
    s_pkt.p[3] = potByte[3];   // CH4  = A3 + trim
    s_pkt.p[4] = sw1b;         // CH5  = SW1
    s_pkt.p[5] = sw2b;         // CH6  = SW2
    s_pkt.p[6] = auxb;         // CH7  = AUX (NOVO no v3)
    s_pkt.p[7] = 128;          // CH8  = reservado neutro
    s_pkt.s1   = 128;          // CH9  = reservado neutro
    s_pkt.s2   = 128;          // CH10 = reservado neutro

    // O write() bloqueia até a transmissão (a 250 kbps payload 10 B ≈ 1.5 ms).
    // Não há AutoAck → sem retentativa → tempo deterministicamente curto.
    s_radio.write(&s_pkt, sizeof(s_pkt));
}
