# Firmwares Legacy

Esta pasta guarda firmwares **anteriores ao upgrade ELRS** mantidos para referência histórica e como fallback.

---

## `NANO_TX_v1_nrf24.ino`

Versão original do firmware do transmissor (Arduino Nano), restaurada a partir do commit `77a5226` ("Refactor TX_NANO and TX_ESP8266 for encoder support", 2025-09-04).

### Por que está aqui

O arquivo `firmware/TX_NANO/NANO_TX/NANO_TX.ino` no `main` atual contém **código do FC ESP32 por engano** (acidente provável durante o commit `44251f1`). Esse arquivo legacy é o **NANO_TX original real** que casa com o protocolo UART consumido pelo `firmware/TX_ESP8266/TX_ESP8266.ino`.

### Características

- nRF24L01: CE=D8, CSN=D7, endereço `"00001"`, canal 76, 250 kbps, sem AutoAck
- 8 pots (A0..A7), 2 switches (D4, D5), botão CAL (D6)
- Encoder rotativo (CLK=D2, DT=D3, SW=D10) com ISR e debounce temporal
- Payload UART para ESP8266 HUD: `AA 55 | len(11) | {ch[8], sw, seqShort, seqLong} | crc8(0x8C)` a 38400 baud
- Calibração min/max dos pots gravada em RAM (não persiste)
- Taxa de envio: 100 Hz (10 ms)

### Substituído por

[`firmware/TX_NANO_v2/NANO_TX.ino`](../TX_NANO_v2/NANO_TX.ino) — mantém o envio nRF24 (compatível com os RX antigos) e **adiciona envio CRSF** ao novo módulo Heltec V2 + ELRS.

### Quando usar este legacy

- Se você reverter o upgrade ELRS e quiser voltar ao sistema antigo
- Como referência para entender a estrutura de pacotes pré-ELRS
- Para flashar num Nano de backup que ainda vai ser usado com o ESP8266 HUD antigo
