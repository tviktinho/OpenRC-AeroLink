---
name: project-user-hardware-inventory
description: Hardware RF que o usuário já tem (TX e RX) e a qual subprojeto cada peça pertence — pra responder rápido "isso serve pra X?"
metadata:
  type: project
---

Inventário de hardware RF do usuário ([[project-openrc-aerolink]]) e a capacidade real de cada peça.

| Peça | Chip RF | Banda | Cobre quais protocolos? | Subprojeto |
|---|---|---|---|---|
| Arduino Nano + nRF24L01 PA+LNA (`firmware/WALL-E/NANO_TX.ino`) | nRF24L01 | 2.4 GHz | nRF24 nativo + XN297 (Bayang, SymaX, V2x2, CX-10, H8_3D) | Controle de mão / TX multiprotocolo de drone ([[project-nano-multiprotocol-drone-tx]]) |
| Heltec WiFi LoRa 32 V2 (`firmware/Heltec_V2_TX_custom/`, `firmware/RF_LinkTest_Heltec/`) | SX1276 | sub-GHz (rodando 915 MHz) | LoRa custom; em tese ELRS 900 MHz com firmware pronto | Link LoRa custom de longo alcance ponto-a-ponto Heltec↔Heltec |
| ESP32-C3 Super Mini (RISC-V, USB-C nativo) | apenas MCU | — | + nRF24L01 externo via SPI = receptor caseiro V2 → SBUS pra Matek (`firmware/ESP32C3_RX_SBUS/`) | RX nRF24 → SBUS pra Matek F411-RX (alternativa ao CC2500 interno) |
| Taranis QX7 (TX comercial dele) | CC2500 (interno) | 2.4 GHz | FrSky D8 / D16 | Para voar a Mobula (FrSky) e qualquer drone FrSky |
| Drone com FC **Matek F411-RX** (target `MATEKF411RX`, BF 4.1.6) — usuário chamou de "Mobula" mas FC indica quad maior, ver [[project-user-existing-drone-qx7]] | CC2500 confirmado | 2.4 GHz | Recebe FrSky D8/D16 SPI (definir via `get rx_spi_protocol`) | Voo de FPV, pilotada pelo QX7 |

**Combinações que funcionam (ponta a ponta):**
- Nano+nRF24 ↔ outro nRF24/XN297 (receptor caseiro do projeto OU drone Bayang/SymaX/etc. quando comprar Eachine E010)
- Heltec V2 ↔ outra Heltec V2 (link LoRa custom)
- QX7 (CC2500) ↔ Mobula (CC2500)

**Combinações que NÃO funcionam (por hardware):**
- Nano+nRF24 → Mobula FrSky (chip diferente, ver [[reference-rf-emulation-boundaries]])
- Heltec V2 → Mobula (banda diferente: 915 MHz vs 2.4 GHz)
- Heltec V2 → qualquer drone comercial 2.4 GHz

**How to apply:** quando o usuário perguntar "X serve pra Y?", consultar essa tabela em vez de derivar do zero. Se ele mencionar nova peça de hardware, atualizar a tabela aqui. Modelo exato da Mobula ainda pendente — instruções de identificação estão em [[project-user-existing-drone-qx7]].
