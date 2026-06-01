---
name: project-nano-multiprotocol-drone-tx
description: Subprojeto — receptor caseiro ESP32 (nRF24 V2 → SBUS) para a Matek F411-RX dele, no lugar de emular protocolos comerciais
metadata:
  type: project
---

**Pivot (2026-05-30):** o usuário descartou a ideia de emular protocolos comerciais (Bayang/SymaX/etc.) no Nano TX. Em vez disso, vai construir um **receptor caseiro nRF24 → SBUS** com ESP32-C3, plugado no UART1 da Matek F411-RX dele ([[project-user-existing-drone-qx7]]). Assim, o TX nRF24 dele (NANO_TX_V2 protocolo) controla a Mobula via Matek.

**ESCOPO (atualizado 2026-05-31):** este subprojeto do receptor SBUS **NÃO entra no TCC** — é hobby do usuário pra pilotar a Mobula com o controle dele. O TCC é apenas o NANO_TX V2 (controle de mão + protocolo nRF24 próprio). Ver [[feedback-direct-tone-hobby]] — tom direto, sem teoria desnecessária quando trabalhando no receptor SBUS.

**Why:** evita o impasse "Mobula é FrSky, nRF24 não fala FrSky" reaproveitando o NANO_TX V2 já existente. Documentação formal só se ele pedir depois.

**Contrato V2 (extraído de `firmware/NANO_RX/NANO_RX/NANO_RX_v2.0.ino` e `firmware/14-BRIZA/14-BRIZA.ino`):**
- nRF24: addr `"00001"`, channel 76, 250 kbps, AutoAck OFF
- struct: `PacketRF { uint8_t p[8]; uint8_t s1; uint8_t s2; }` (10 bytes)
- canais: `p[0]=THR`, `p[1]=RUD`, `p[2]=AIL`, `p[3]=ELE`, `p[4..7]=aux`

**Entregue (2026-05-30):**
- v1 Arduino IDE (ESP32 DevKit): `firmware/ESP32_RX_SBUS/ESP32_RX_SBUS.ino`. Mantido como referência.
- v2 PlatformIO (ESP32-C3 Mini ← hardware real do usuário): `firmware/ESP32C3_RX_SBUS/`
  - `platformio.ini` (board `esp32-c3-devkitm-1`, `ARDUINO_USB_CDC_ON_BOOT=1`, lib `nrf24/RF24 @ ^1.4.10`)
  - `src/main.cpp` (mesmo código adaptado: Serial1 em vez de Serial2, SPI custom via GPIO matrix)
  - Pinagem C3: SCK=4, MISO=5, MOSI=6, CSN=7, CE=10, SBUS_TX=GPIO21 (UART1 invertido)
  - Evita strapping (0,2,8,9) e USB nativo (18,19)
  - Comandos: `pio run`, `pio run -t upload`, `pio device monitor`
  - Build inicial em 2026-05-30 (primeira compilação baixa toolchain RISC-V).

**Pendente (espera testes do usuário):**
- Bench: monitor SBUS no Serial do ESP — esperar `rx > 0` ao mexer sticks
- Conexão na Matek + config CLI (`feature -RX_SPI`, `set serialrx_provider = SBUS`, etc.)
- Voo amarrado primeiro
- Possíveis ajustes: latência (reduzir SBUS_INTERVAL_US pra 9000), PA_HIGH/MAX no alcance, channel map se canais saírem invertidos

**RESULTADO FINAL (2026-06-01):** SBUS NÃO funcionou com nenhuma config testada (HardwareSerial Arduino, IDF UART_NUM_1, UART_NUM_0, RMT bit-bang, todas as combinações de polaridade) — a Matek do user (que ele acredita ser Mobula6 com target MATEKF411RX flasheado) aceita SBUS de RX comercial mas rejeita o do ESP32-C3. Causa raiz não diagnosticada com certeza (suspeita: timing UART do C3, ou pino físico R1 não conectado direto ao PA10 nessa variante AIO).

**SOLUÇÃO ADOTADA:** trocou pra **PPM** (single-wire, sem polaridade, sem paridade). Funcionou de primeira. Firmware atual em `firmware/ESP32C3_RX_SBUS/src/main.cpp` (nome da pasta mantido por preguiça) usa **RMT bit-bang PPM** no GPIO21, 8 canais, ~50Hz. BF configurado com `feature RX_PPM` + Receiver Mode PPM. Pad físico: PPM ou R2 (PA03).

**Mapeamento atual (V2 → PPM):**
- CH1 Roll  ← p.p[3] (ELE) — trocado com Pitch a pedido
- CH2 Pitch ← p.p[2] (AIL) — trocado com Roll
- CH3 Throttle ← p.p[0]
- CH4 Yaw ← p.p[1]
- CH5 Arm ← s1
- CH6 Mode ← s2
- CH7/8 ← p.p[4], p.p[5]

**Restrição ainda ativa** ([[hardware-rf-chip-constraint]], [[reference-rf-emulation-boundaries]]): essa solução **não emula protocolos comerciais** — continua sendo o protocolo nRF24 V2 próprio do projeto. Para pilotar drones FrSky/Flysky/etc. de outros donos, a barreira física continua valendo.

**How to apply:** se o usuário voltar com bugs do firmware, abrir o sketch e a tabela de diagnóstico que entreguei (rx=0 vs SBUS inválido vs failsafe travado). Se quiser otimizar latência, mexer SBUS_INTERVAL_US (de 14000 pra 9000 µs) e/ou reduzir `vTaskDelay`/sleep no loop. Pra alcance maior, subir `setPALevel` pra HIGH ou MAX e validar que o cap eletrolítico do nRF24 dá conta.
