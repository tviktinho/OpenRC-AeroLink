---
name: hardware-rf-chip-constraint
description: O nRF24 só consegue transmitir protocolos nRF24/XN297. FrSky/Flysky/Spektrum exigem outros chips.
metadata:
  type: project
---

O hardware do controle do usuário ([[project-openrc-aerolink]]) é nRF24L01 PA+LNA. Isso limita drasticamente quais protocolos de RC ele pode transmitir:

| Protocolo / família | Chip necessário | Cabe no projeto? |
|---|---|---|
| nRF24 nativo, XN297 (emulado), Bayang, SymaX, V2x2, CX-10, H8/H8_3D, Hisky, MJX, ASSAN | nRF24L01 | ✅ SIM |
| FrSky D/X, SFHSS, Corona | CC2500 | ❌ NÃO |
| Flysky AFHDS / AFHDS-2A, Hubsan (alguns) | A7105 | ❌ NÃO |
| Spektrum DSM2 / DSMX, Walkera | CYRF6936 | ❌ NÃO |

**Why:** cada chip cobre uma faixa/modulação diferente — não tem como o nRF24 emular FSK do CC2500 ou OOK do A7105. O nRF24 *consegue* emular XN297 (chip clone usado em drones chineses) porque a modulação GFSK é a mesma; só muda preâmbulo, address, scrambling e CRC, que dá pra fazer em software desligando os recursos do nRF24 e tratando como buffer cru.

**How to apply:** se o usuário mencionar um drone FrSky/Flysky/Spektrum como alvo, **lembrar imediatamente que está fora do escopo** e sugerir usar o Taranis QX7 dele para esses. Para qualquer modelo de drone que ele mencionar, validar primeiro a qual família de protocolo pertence antes de tentar implementar. Lista do Multiprotocol-TX-Module ou nRF24_multipro são as fontes para mapear modelo→protocolo→chip.
