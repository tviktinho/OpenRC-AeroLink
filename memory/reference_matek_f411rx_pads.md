---
name: reference-matek-f411rx-pads
description: Pads expostos da Matek F411-RX e o que cada um faz — pra não confundir SBUS_OUT com SBUS_IN.
metadata:
  type: reference
---

Hardware: Matek F411-RX (FC do usuário, ver [[project-user-existing-drone-qx7]]).

| Pad | O que é | Direção | Uso |
|---|---|---|---|
| `R1` | UART1 RX | entrada | RX externo (CRSF, iBUS, SBUS) ou GPS |
| `T1` | UART1 TX | saída | telemetria, GPS, MSP |
| `R2` | UART2 RX | **entrada** | **RX externo SBUS** ← onde o usuário soldou GPIO21 do ESP32-C3 |
| `T2` | UART2 TX | saída | telemetria, MSP |
| `SBUS` | SBUS_OUT do RX SPI interno | **saída** | encadear SBUS pra gimbal/2º controlador. **NÃO É ENTRADA**. |
| `5V`, `4V5`, `VBAT`, `GND` | alimentação | — | — |
| `BZ+`, `BZ-` | buzzer | saída | — |
| `Cam`, `VTX` | vídeo FPV | — | — |
| `RX-Bind` | botão de bind do RX interno | entrada | — |

**Why:** o usuário perguntou se em vez de R2 seria melhor soldar no pad rotulado SBUS. **Resposta: NÃO** — o SBUS pad é SBUS_OUT (saída do CC2500 interno). Soldar lá como input não funciona.

**How to apply:** se o usuário voltar a perguntar sobre pads da Matek, consultar essa tabela. A entrada SBUS externa é só pelo R2 (UART2 RX). UART1 (R1/T1) também é entrada válida se mover serial config.
