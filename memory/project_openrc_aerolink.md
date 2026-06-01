---
name: project-openrc-aerolink
description: Visão geral do projeto OpenRC-AeroLink e do hardware do controle (Nano TX)
metadata:
  type: project
---

**OpenRC-AeroLink**: sistema RC modular open-source, base do TCC do usuário ([[user-profile]]).

**Hardware do controle de mão (NANO_TX)** — arquivo de referência: `firmware/WALL-E/NANO_TX.ino`:
- Arduino Nano (ATmega328P, 16 MHz)
- nRF24L01 PA+LNA em CE=D8, CSN=D7, MOSI=D11, MISO=D12, SCK=D13
- 7 potenciômetros em A0–A6 (lidos como 8 bits, `analogRead >> 2`)
- 4 switches em D2–D5 (INPUT_PULLUP, ativo LOW)
- Biblioteca RF24 (tmrh20), canal 76, endereço `"0001"`, PA_MIN, 250 kbps, AutoAck OFF
- Payload próprio: `struct Packet { uint8_t p[7]; uint8_t s[4]; }` (11 bytes), TX a 50 Hz

Detalhe HW: nRF24 PA+LNA exige capacitor de 10–100 µF entre VCC/GND, senão dá reset (já documentado no .ino, é um pé na orelha conhecido do usuário — não precisa explicar de novo).

**Why:** TCC do usuário gira em torno desse projeto.
**How to apply:** ao escrever firmware NOVO para o Nano TX (ex.: módulo multiprotocolo de drones — ver [[project-nano-multiprotocol-drone-tx]]), reaproveitar leitura de pots/switches e a pinagem nRF24 já existente. Não reinventar a pinagem.

Sub-iniciativas / módulos do projeto: ESP32 FC ([[..]]), ESP32 RX, MEGA RX, ESP8266 HUD, WALL-E tank, e agora a iniciativa TX-multiprotocolo de drones nRF24.
