---
name: user-profile
description: Quem é o usuário — perfil acadêmico e nível técnico — para guiar tom e profundidade das respostas
metadata:
  type: user
---

Estudante de engenharia trabalhando no TCC. O TCC é centrado no projeto **OpenRC-AeroLink** (ver [[project-openrc-aerolink]]).

Tem conhecimento prático de eletrônica embarcada: já construiu controle RC DIY com Arduino Nano + nRF24L01 PA+LNA, conhece SPI, ADC, INPUT_PULLUP, struct packing. Está confortável lendo C/C++ para Arduino.

Trabalha em Windows (PowerShell) com Arduino IDE / PlatformIO.

**Como adaptar respostas:** pode usar termos técnicos sem definir os triviais (SPI, ADC, PWM, struct, payload). Não precisa explicar Arduino básico. Explicar bem coisas RF (canais, hopping, whitening, scrambling, payload de protocolo) — essas são novas para ele e são o foco do aprendizado. Ver [[feedback-explain-why]] para o estilo pedagógico que ele quer.

**Lacunas observadas (2026-05-30):** o usuário ainda está construindo o modelo mental das **camadas do stack RC** — confundiu protocolo de RF (D8/D16/Bayang/AFHDS) com protocolo de saída do RX→FC (SBUS/PPM/iBUS/CRSF). Quando for falar de qualquer protocolo, ancorar primeiro em qual camada ele vive (RF "no ar" vs. fio "RX→FC"). Também precisou de ajuda pra identificar chip da FC (confundiu OSD AT7456E com chip de RF).
