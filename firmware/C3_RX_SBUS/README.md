# C3_RX_SBUS — Receptor nRF24 → SBUS para Betaflight

Receptor caseiro baseado em **ESP32-C3 Super Mini** + **nRF24L01 PA+LNA**.  
Recebe comandos do **NANO_TX** e gera stream **SBUS** para a **Matek F411-RX** (Betaflight).

---

## Esquema de Ligação

### ESP32-C3 ↔ nRF24L01 PA+LNA

```
┌─────────────────────────────────────────┐
│          ESP32-C3 Super Mini            │
│                                         │
│  nRF24L01 PA+LNA   │  Pino ESP32-C3    │
│─────────────────────┼───────────────────│
│  VCC               │  3V3              │
│  GND               │  GND              │
│  CE                │  GPIO10           │
│  CSN               │  GPIO7            │
│  SCK               │  GPIO4            │
│  MOSI              │  GPIO6            │
│  MISO              │  GPIO5            │
│  IRQ               │  (não conectar)   │
│                                         │
│  ⚠️ Cap 10–100µF eletrolítico entre    │
│     VCC e GND do nRF24 (obrigatório!)  │
└─────────────────────────────────────────┘
```

### ESP32-C3 ↔ Matek F411-RX

```
┌─────────────────────────────────────────┐
│  ESP32-C3           │  Matek F411-RX    │
│─────────────────────┼───────────────────│
│  GPIO21 (SBUS TX)  │  RX2 (pad R2)     │
│  GND               │  GND              │
│  (opc) 5V          │  5V               │
└─────────────────────────────────────────┘

  ⚠️ GND COMUM É OBRIGATÓRIO, mesmo com
     ESP alimentado por USB separado.
```

### Diagrama Geral

```
                     2.4 GHz
┌──────────┐      ┌──────────────┐  GPIO21    ┌──────────────┐
│ NANO TX  │ )))  │  ESP32-C3    │───SBUS────→│ Matek F411   │
│ (controle)│      │  + nRF24L01  │            │ (Betaflight) │
└──────────┘      └──────────────┘            └──────────────┘
  250 kbps           ~71 Hz SBUS               AETR, SBUS
  ch76, "00001"      100kbps 8E2 inv           serialrx_inverted=OFF
```

---

## Config Betaflight

Na aba **Ports**:
- UART2 (ou a UART do pad R2): **Serial RX** ativado

Na aba **Configuration**:
| Parâmetro | Valor |
|---|---|
| Receiver Mode | **Serial** |
| Serial Receiver Provider | **SBUS** |
| Channel Map | **AETR1234** |

Na CLI:
```
set serialrx_inverted = OFF
set serialrx_halfduplex = OFF
save
```

---

## Mapeamento de Canais

| Canal SBUS | Função | Origem no PacketRF |
|---|---|---|
| CH1 | Roll | `p[2]` (AIL) |
| CH2 | Pitch | `p[3]` (ELE) |
| CH3 | Throttle | `p[0]` (THR) |
| CH4 | Yaw | `p[1]` (RUD) |
| CH5 | AUX1 (Arm) | `s1` |
| CH6 | AUX2 (Mode) | `s2` |
| CH7–10 | AUX3–6 | `p[4..7]` |
| CH11–16 | Neutro | 992 (centro) |

---

## Compilar e Gravar

```bash
# Compilar
pio run

# Gravar
pio run -t upload

# Monitor Serial (debug via USB-C)
pio device monitor -b 115200

# Tudo junto
pio run -t upload -t monitor
```

---

## Failsafe

Se ficar **>200 ms** sem pacote do TX:
- Throttle → MIN (172)
- Arm → OFF
- Demais canais → centro (992)
- Flag `failsafe` + `frame_lost` no SBUS
- Betaflight entra em failsafe conforme configurado

---

## Hardware Necessário

| Componente | Quantidade |
|---|---|
| ESP32-C3 Super Mini | 1 |
| nRF24L01 PA+LNA | 1 |
| Cap eletrolítico 10–100 µF | 1 |
| Fios/jumpers | ~10 |
| Matek F411-RX (ou FC com SBUS) | 1 |
