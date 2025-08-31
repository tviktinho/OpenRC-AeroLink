# NANO_TX — Transmissor

Firmware do transmissor principal do **OpenRC AeroLink**.

## Funções
- Leitura de **8 potenciômetros** (eixos analógicos).
- Leitura de **2 switches**.
- Envio de pacotes via **nRF24L01 PA/LNA** (SPI).
- Saída **UART** para HUD (ESP8266_TX).
- Botão de **calibração** com persistência opcional em EEPROM.

## Pinagem (ajuste conforme PCB)
- **nRF24L01:** CE=D9, CSN=D10, MOSI=D11, MISO=D12, SCK=D13.
- **Pots:** A0..A7.
- **S1/S2/Mode:** digitais com `INPUT_PULLUP`.
- **UART HUD:** SoftwareSerial (ex.: D6=TX → ESP RX).
- **Botão CAL:** digital com `INPUT_PULLUP`.

## Bibliotecas
- [RF24](https://github.com/nRF24/RF24)
- EEPROM (opcional)
- SoftwareSerial (se necessário)

## Observações
- Frequência recomendada de envio: **50–100 Hz**.
- Usar capacitores de desacoplamento no nRF24L01 PA/LNA.
