# ESP8266_TX — HUD do Transmissor

Firmware do HUD (Head-Up Display) no transmissor.

## Funções
- Lê pacotes CSV via **UART** do NANO_TX.
- Renderiza dados em **duas telas OLED 0,96"**.
- Feedback sonoro em **buzzer ativo (3.3V)**.
- Animação de **calibração** e alerta de perda de link.

## Pinagem
- **OLED Interna:** SDA=D6, SCL=D5 (U8G2_R0).
- **OLED Externa:** SDA=D2, SCL=D1 (U8G2_R2).
- **Buzzer:** D7 (GPIO13).
- **UART RX:** GPIO (ajustar conforme montagem, baud=115200).

## Bibliotecas
- [U8g2](https://github.com/olikraus/u8g2)

## Observações
- Usar desenho em buffer parcial para não travar a UART.
- HUD pode limitar framerate (~20 Hz).
