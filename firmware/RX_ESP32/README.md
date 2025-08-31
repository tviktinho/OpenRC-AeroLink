# ESP32_RX — Receptor Moderno

Receptor oficial e recomendado para o **OpenRC AeroLink**.

## Funções
- Recebe pacotes via **nRF24L01 (PA/LNA)**.
- Gera saídas **PWM (ledcWrite)** para servos/ESC.
- Suporte a **PPM-SUM** (pino único).
- Pode atuar como **ponte USB** para simulador PC.
- Preparado para **telemetria Wi-Fi/OTA**.

## Pinagem (VSPI padrão)
- **nRF24L01:** CLK=18, MISO=19, MOSI=23, CSN=5, CE=17.
- **PWM out:** 25, 26, 27, 14, 12, 13 (ajustável).
- **PPM-SUM:** GPIO4.
- **USB:** serial nativa do ESP32.

## Bibliotecas
- RF24
- driver/ledc (nativo ESP32)

## Observações
- Use fonte estável para alimentar PA/LNA.
- Aproveite timers do ESP32 para pulsos estáveis.
