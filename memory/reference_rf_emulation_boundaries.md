---
name: reference-rf-emulation-boundaries
description: O que o nRF24 pode (XN297) e não pode (FrSky/Flysky/Spektrum) emular em software, e por quê — material de fundamentação do TCC.
metadata:
  type: project
---

Discussão técnica que pode virar capítulo de fundamentação teórica do TCC ([[project-openrc-aerolink]], [[project-nano-multiprotocol-drone-tx]]). Estende [[hardware-rf-chip-constraint]] com o "porquê" físico.

**Regra prática:** o nRF24L01 consegue emular por software qualquer protocolo cuja **camada física (PHY)** seja **GFSK 1 Mbps (ou 250 kbps), deviation ±160 kHz, channel bandwidth ~1 MHz, grade de canais de 1 MHz**. Tudo o que está fora desses parâmetros físicos exige outro chip — não tem software que resolva.

**O que cabe no nRF24 (PHY idêntica):**
- nRF24 nativo
- XN297 (clone chinês usado em drones de brinquedo). Diferenças com nRF24 estão APENAS no enquadramento: preâmbulo (38 zeros vs 1), scrambling do payload via LUT, CRC com seed diferente, address embaralhado. Tudo solucionável desligando recursos automáticos do nRF24 e tratando payload como buffer cru via SPI.
- Protocolos que rodam em cima: Bayang, SymaX, V2x2, CX-10, H8_3D, Hisky, MJX.

**O que NÃO cabe no nRF24 (PHY incompatível):**

| Protocolo | Chip exigido | Data rate | Deviation | BW canal | Por que nRF24 não consegue |
|---|---|---|---|---|---|
| FrSky D8 | CC2500 | 9.6 kbps | ±57 kHz | ~232 kHz | nRF24 mínimo é 250 kbps; sinal sai ~3× mais largo do que o RX espera, é rejeitado como ruído fora de banda |
| FrSky D16 / SFHSS | CC2500 | similar a D8 + telemetria | — | — | mesmas razões + telemetria bidirecional complexa |
| Flysky AFHDS-2A | A7105 | — | — | — | OOK/FSK em parâmetros diferentes |
| Spektrum DSMX | CYRF6936 | — | — | — | DSSS, não GFSK |
| ELRS | SX1276/SX1281 | LoRa | — | — | LoRa (CSS), não GFSK |

**Analogia para explicar didaticamente:** o RX FrSky é alguém ouvindo FM com filtro estreito (~232 kHz). O nRF24 só consegue falar com "megafone de banda larga" (~1 MHz). A voz vaza pras estações vizinhas e a estação alvo descarta como ruído. O **conteúdo** (bytes) não importa; o **formato** (PHY) não bate.

**Por que isso é interessante pro TCC:** transforma a limitação de hardware em conteúdo acadêmico. Estrutura sugerida da fundamentação teórica do TCC:
1. Stack/camadas em comunicação RC (RF ↔ enquadramento ↔ payload ↔ saída SBUS/PPM)
2. Tabela de chips × modulações × parâmetros físicos
3. Caso de estudo: emulação possível (XN297 ⊂ nRF24) — mostra COMO
4. Caso de estudo: emulação impossível (FrSky D8 ⊄ nRF24) — mostra POR QUE
5. Implementação prática: subset que cabe no hardware (Bayang/SymaX/V2x2)

**How to apply:** se o usuário voltar a perguntar sobre "emular protocolo X com nRF24", verificar nessa tabela / regra. Se a PHY bate, é factível em software; se não bate, explicar a barreira física e não prometer milagre.
