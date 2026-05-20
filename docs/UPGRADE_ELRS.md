# OpenRC AeroLink — Upgrade ELRS

Documento mestre do upgrade do link de rádio do **OpenRC AeroLink** de **nRF24L01 (2.4 GHz)** para **ExpressLRS (ELRS) 900 MHz**.

> 🎯 **Motivação**: nRF24 funciona, mas tem alcance limitado (~500-800 m com PA/LNA em condições boas) e protocolo proprietário. ELRS 900 MHz é open-source, oferece **alcance de vários km**, FHSS, telemetria bidirecional padrão e ecossistema enorme.

---

## 📐 Arquitetura geral

### Antes (sistema atual)

```
                            2.4 GHz nRF24L01
       ┌─────────────┐                              ┌─────────────────┐
       │             │   struct {p[8], s1, s2}      │  RX antigos:    │
       │  NANO_TX    ├──────────────────────────────►  • NANO_RX      │
       │             │   ch76, 250kbps, "00001"     │  • ESP32 FC     │
       └──────┬──────┘                              │  • MEGA_RX      │
              │                                     │  • 14-BRIZA     │
              │  UART 38400 baud                    └─────────────────┘
              │  AA 55 | len | payload | crc8
              ▼
       ┌─────────────┐
       │  ESP8266    │   HUD com 2 OLEDs + encoder + buzzer
       │  HUD        │
       └─────────────┘
```

### Depois (alvo da Fase 2 — TCC)

```
              ┌─────────────┐
              │             │── nRF24 ──► RX antigos (legacy, mantido)
              │  NANO_TX v2 │
              │             │── CRSF ──┐ 420 000 baud, 16 canais
              └─────────────┘          │
                                       ▼
                                ┌──────────────────────┐
                                │  Heltec V2 (ESP32 +  │
                                │  SX1276 900 MHz)     │     900 MHz LoRa
                                │                      ├────► RX ELRS
                                │  Firmware custom:    │      (no avião)
                                │  • protocolo ELRS    │
                                │  • 2 OLEDs           │
                                │  • encoder/buzzer    │
                                │  • menu (vindo do    │
                                │    ESP8266 antigo)   │
                                └──────────────────────┘

       (ESP8266 antigo: REMOVIDO da PCB do controle)
```

### Transição (Fase 1 — onde estamos agora)

Na Fase 1 usamos **firmware oficial do ELRS** no Heltec (UI nativa nas OLEDs, sem nossos cards/menus). O ESP8266 antigo pode continuar funcionando em paralelo se você quiser manter o HUD, OU já pode ser removido.

```
              ┌─────────────┐
              │             │── nRF24 ──► RX antigos (sem mudança)
              │  NANO_TX v2 │
              │             │── CRSF ──┐
              └─────┬───────┘          │
                    │ (opcional)       ▼
                    │            ┌──────────────────────┐
                    │            │  Heltec V2 + ELRS    │
                    │            │  oficial (UI nativa) ├────► RX ELRS 900
                    │            └──────────────────────┘
                    ▼
              ┌─────────────┐
              │  ESP8266    │   (opcional, mantido p/ confirmação que
              │  HUD        │    UART não regrediu — frame é diferente,
              └─────────────┘    HUD não vai mostrar dados, mas vai
                                 ficar mudo até ser removido)
```

> 💡 Em rigor, o ESP8266 antigo **espera frame UART em 38400 baud com prefixo `AA 55`**, mas o NANO_TX v2 manda **CRSF em 420000 baud**. Logo o HUD antigo fica "mudo" — não dá merge fácil sem mudar tudo no ESP8266. Por isso o usuário decidiu **aposentar o ESP8266 já na Fase 1**.

---

## 🔑 Decisões técnicas

| Decisão | Escolha | Justificativa |
|---|---|---|
| Banda RF | **900 MHz (SX1276)** | Heltec V2 já tem; melhor alcance/penetração p/ aeromodelo |
| Hardware TX RF | **Heltec WiFi LoRa 32 V2** | Placa que o usuário já possui |
| Hardware RX RF | **A comprar (900 MHz)** | Happymodel EPW6, BetaFPV SuperD 900, Matek R24-D 900 |
| Protocolo TX↔módulo | **CRSF v3 @ 420 000 baud** | Padrão da indústria, suportado por todos os módulos ELRS/Crossfire |
| nº de canais CRSF úteis | **8** (7 pots + 1 switch agregado) | Simplicidade; CH9..CH16 reservados |
| Compat com RX antigos | **Mantida via nRF24 paralelo** | Zero downtime durante a transição |
| Stack ELRS na Fase 1 | **Oficial** (DIY_900_TX_ESP32_SX127x_RFM95 + layout custom) | Valida o RX comprado rapidamente |
| Stack ELRS na Fase 2 | **Firmware custom Arduino** | Liberdade total de UI e recursos para o TCC |
| Destino do ESP8266 | **Aposentar** (Heltec assume HUD) | Reduz complexidade, libera espaço na PCB |

---

## 🧩 Estrutura de pastas após o upgrade

```
firmware/
├── legacy/                              # Firmwares anteriores ao upgrade
│   ├── NANO_TX_v1_nrf24.ino             # Versão original (restaurada do git)
│   └── README.md
│
├── TX_NANO_v2/                          # ★ Fase 1 — NOVO
│   ├── NANO_TX.ino                      # nRF24 + CRSF paralelo
│   ├── crsf_tx.h                        # Encoder CRSF reutilizável
│   └── README.md
│
├── Heltec_V2_TX_ELRS/                   # ★ Fase 1 — NOVO (não tem código C++)
│   ├── README.md                        # Passo-a-passo do flash ELRS
│   ├── heltec_v2_sx1276.json            # Layout custom p/ Configurator
│   ├── pinout_diagram.md                # Mapa completo de pinos
│   └── troubleshooting.md               # FAQs e diagnóstico
│
├── Heltec_V2_TX_custom/                 # ☆ Fase 2 (TCC) — A FAZER
│   └── (firmware Arduino próprio quando chegar a hora)
│
├── TX_NANO/                             # ⚠️ NÃO TOCAR (corrompido — código do FC)
├── TX_ESP8266/                          # Mantido como referência (será aposentado)
├── ESP32/, ESP32_RX/, MAVlink/          # FC, sem mudança
├── NANO_RX/, MEGA_RX/, 14-BRIZA/        # RX antigos, sem mudança
└── WALL-E/                              # Subsistema tank drive, sem mudança

docs/
├── UPGRADE_ELRS.md                      # ★ Este arquivo
├── ROADMAP.md                           # Atualizado com fases ELRS
├── PROTOCOLO.md                         # Original (legacy)
├── RELATORIO.md                         # Original (legacy)
└── PCB-RELATORIO.md                     # ⚠️ vazio (corrigir depois)
```

---

## 🚦 Checklist do upgrade (resumo executivo)

### Pré-upgrade
- [ ] Comprar RX ELRS 900 MHz (definir modelo)
- [ ] Comprar antena 900 MHz com conector U.FL (se Heltec não veio com uma)
- [ ] Instalar drivers CP2102 e ExpressLRS Configurator

### Fase 1 — Validação
- [ ] Flash do `firmware/TX_NANO_v2/NANO_TX.ino` no Arduino Nano
  - Lembrar de desconectar TX0 do Heltec durante o upload
- [ ] Conferir LED do Nano piscando (loop a 50 Hz)
- [ ] Conferir RX antigos (NANO_RX, ESP32 FC) **ainda respondendo** (nRF24 preservado)
- [ ] Flash do Heltec V2 com ELRS oficial + layout custom (ver `firmware/Heltec_V2_TX_ELRS/README.md`)
- [ ] Bind do RX 900 MHz com a mesma binding phrase
- [ ] Validar via web UI do Heltec que CH1..CH8 se movem
- [ ] Validar no avião que o RX gera PWM/SBUS/CRSF correto

### Fase 2 — TCC
- [ ] Definir RX final do TCC (mesmo da Fase 1 ou outro)
- [ ] Escrever firmware custom no Heltec
- [ ] Migrar encoder/buzzer/2ª OLED do ESP8266 fisicamente
- [ ] Aposentar ESP8266 da PCB
- [ ] Testar UI completa
- [ ] Atualizar `docs/RELATORIO.md` com a história do upgrade

---

## 🛡 Política de não-regressão

Toda mudança da Fase 1 **NÃO PODE QUEBRAR**:
- ❌ Recepção nRF24 nos RX antigos (NANO_RX, ESP32 FC, 14-BRIZA, MEGA_RX)
- ❌ Funcionamento do subsistema WALL-E (independente, em pasta separada)
- ❌ Estrutura `Packet {p[8], s1, s2}` da nRF24 (endereço, canal, baud rate)

Toda mudança **PODE QUEBRAR** (com aviso prévio):
- ✅ HUD do ESP8266 (frame UART mudou de `AA 55 ...` 38400 para CRSF 420000)
  - Mitigação: ESP8266 pode ser desligado/removido sem prejuízo do link RF
- ✅ Calibração min/max dos pots gravada no Nano (não persistia mesmo, era em RAM)
  - Mitigação: ELRS oferece calibração via web UI no Heltec

---

## 📚 Referências externas

- ExpressLRS docs oficial: https://www.expresslrs.org/
- DIY 900TX guide: https://www.expresslrs.org/quick-start/transmitters/diy900/
- Heltec WiFi LoRa 32 V2 pinout: https://www.espboards.dev/esp32/heltec-wifi-lora-32-v2/
- TBS Crossfire (CRSF) protocol overview: https://www.team-blacksheep.com/products/prod:crossfire_tx
- ExpressLRS Configurator: https://github.com/ExpressLRS/ExpressLRS-Configurator
- ExpressLRS Targets repo: https://github.com/ExpressLRS/targets

---

## 📝 Histórico do documento

| Data | Versão | Mudança |
|---|---|---|
| 2026-05-16 | 1.0 | Criação inicial (Fase 0 + Fase 1 entregues) |
