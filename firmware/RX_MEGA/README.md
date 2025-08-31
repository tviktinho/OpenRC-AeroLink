# MEGA_RX — Receptor Compatível

Firmware para Arduino MEGA usado como receptor.

## Funções
- Recebe pacotes via **nRF24L01**.
- Gera saídas **PWM** para servos/ESC.
- Pode atuar como **ponte para simulador PC** (via Serial).
- Implementa **failsafe** básico.

## Pinagem
- **nRF24L01:** CE/CSN + SPI.
- **PWM:** 6 pinos digitais configurados com Servo.
- **UART PC:** porta USB do MEGA.

## Bibliotecas
- RF24
- Servo

## Observações
- Útil quando ESP32 não está disponível.

# MEGA_SIM — Simulador (vJoy + Python)

Firmware + software para usar o controle em simuladores no PC.

## Funções
- Arduino MEGA atua como **gateway Serial**.
- Script Python (`mega_joystick.py`) converte sinais em entradas de joystick (vJoy).
- Switches podem atuar como botões e teclas do teclado.

## Fluxo
[ TX ] → Rádio → [ RX ] → Serial → [ MEGA_SIM ] → USB → [ PC (Python + vJoy) ]

## Requisitos no PC
- Python 3.12+
- Bibliotecas: `pyserial`, `pyvjoy`, `keyboard`
- vJoy driver instalado

## Script
- `mega_joystick.py` faz mapeamento 0–255 → 1–32767.
- Switches tratados com **cache de estado** (evita pulsos curtos).

## Observações
- Ajustar porta COM no script.
- Testar teclas e botões separadamente antes de usar nos simuladores.
