# NANO_RX — Receptor (Legado)

Versão inicial de RX usada na Etapa 2 do projeto.

## Funções
- Recebe pacotes via **nRF24L01**.
- Reconstrói 8 eixos + switches.
- Saída para:
  - **PWM** (servos/ESC) ou
  - **PPM-SUM** (sinal único).
- Failsafe simples (neutralizar canais).

## Pinagem
- **nRF24L01:** conforme ligação na protoboard/PCB.
- **PWM out:** 8 pinos digitais.
- **PPM-SUM:** pino único (opcional).

## Bibliotecas
- RF24
- Servo

