#include "crsf.h"
#include "crsf_tx.h"
#include "inputs.h"   // potByte, sw1b, sw2b, auxb

static uint16_t s_channels[CRSF_NUM_CHANNELS];
static uint8_t  s_frame[CRSF_FRAME_SIZE];

void crsf_setup() {
    // A HW Serial é DEDICADA ao CRSF a partir daqui. Quem precisar de prints
    // de debug tem que fazer ANTES desse setup() (no scanner I2C, por exemplo)
    // — depois disso, qualquer Serial.print() bagunça o frame CRSF.
    Serial.begin(CRSF_BAUDRATE);
}

void crsf_send_frame() {
    // Montagem dos 16 canais — mesmos slots da PacketRF:
    s_channels[0] = crsf_byte_to_channel(potByte[0]);   // CH1  A0
    s_channels[1] = crsf_byte_to_channel(potByte[1]);   // CH2  A1
    s_channels[2] = crsf_byte_to_channel(potByte[2]);   // CH3  A2
    s_channels[3] = crsf_byte_to_channel(potByte[3]);   // CH4  A3
    s_channels[4] = crsf_sw_to_channel(sw1b);            // CH5  SW1
    s_channels[5] = crsf_sw_to_channel(sw2b);            // CH6  SW2
    s_channels[6] = crsf_sw_to_channel(auxb);            // CH7  AUX
    // CH8..CH16: sem dado → centro (1500 µs equivalente). Igual ao v2 fazia
    // pros CH11..CH16, agora também pra CH8..CH10 (no v2 esses eram pots,
    // hoje sumiram com os trims tomando o lugar lógico).
    for (uint8_t i = 7; i < CRSF_NUM_CHANNELS; i++) {
        s_channels[i] = CRSF_CHANNEL_VALUE_MID;
    }

    const size_t n = crsf_build_rc_frame(s_frame, s_channels);
    Serial.write(s_frame, n);
}
