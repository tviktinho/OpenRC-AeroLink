---
name: project-user-existing-drone-qx7
description: O usuário já tem um micro drone bindado no Taranis QX7 dele em protocolo FrSky — possível conflito com o subprojeto nRF24.
metadata:
  type: project
---

**Fato (2026-05-30):** o usuário relatou que o drone que ele "tem em mãos" para o subprojeto multiprotocolo ([[project-nano-multiprotocol-drone-tx]]) atualmente é pilotado pelo **Taranis QX7 dele em protocolo FrSky**. Modelo da FC ainda desconhecido — usuário perguntou "como ter certeza do modelo da minha FC?".

Drone é uma **Happymodel Mobula** (modelo exato — 6/6HD/7/7HD/8 — ainda não confirmado). O usuário localizou um chip "AT7456E N9H04AA" na FC (= OSD, não é RF), depois confirmou chip **CC2500** soldado perto da antena.

**Betaflight Configurator (2026-05-30):**
- Configurator 10.10.0, Firmware 4.1.6 BTFL
- **Target: `MTKS/MATEKF411RX(STM32F411)`** = FC **Matek F411-RX** (MCU STM32F411, RX SPI FrSky integrado via CC2500)

⚠️ **Inconsistência a investigar:** Matek F411-RX é FC standalone para quads de 130–220mm, NÃO é típica de Mobula (que usa AIO Happymodel com target `HAPPYMODELMOBULA6/7/HD`). Possibilidades: o drone não é Mobula (confusão), o usuário tem dois drones distintos, ou a FC foi substituída. Aguardando wheelbase e diâmetro da hélice pra confirmar.

**Conclusão sobre protocolo:** confirmado FrSky (CC2500). Precisa rodar `get rx_spi_protocol` no CLI pra saber D8 (`FRSKY_D`) ou D16 (`FRSKY_X`). Independente disso, **fora do escopo nRF24**.

**Nota Betaflight 4.1.6 (importante):** a CLI `set rx_spi_protocol = NONE` NÃO existe nessa versão (só >= 4.2). A lista de valores permitidos é fechada (V202_250K, SYMA_X, ..., FRSKY_D, FRSKY_X, FLYSKY, etc.) — sem `NONE`. Para desabilitar o RX SPI, basta `feature -RX_SPI` (que retorna `Disabled RX_SPI`); o valor de `rx_spi_protocol` é ignorado quando a feature está desligada. Não tentar setar `NONE` no 4.1.x — vai dar `###ERROR: INVALID VALUE###`.

**Implicação:** se o protocolo no menu do QX7 for FrSky D8/D16 puro (módulo interno do QX7 = CC2500), o drone NÃO é compatível com nRF24 — ver [[hardware-rf-chip-constraint]]. Caso esteja em MULTI/Bayang via Multiprotocol Module externo na baia JR, então é nRF24 disfarçado e o protocolo já está identificado.

**Aguardando do usuário:**
- Protocolo exato exibido no MODEL SETUP do QX7 (FrSky D8 / FrSky D16 / MULTI/<algo> / Flysky AFHDS-2A / ...)
- Marca/modelo do drone e da FC
- Foto do chip de RF da FC (NRF24L01+/BK2421/XN297 → cabe; CC2500/A7105/CYRF → não cabe)

**How to apply:** não escrever código antes de resolver isso. Se confirmar CC2500, recomendar Eachine E010 (Bayang) como alvo do TCC — atalho honesto e bem documentado.
