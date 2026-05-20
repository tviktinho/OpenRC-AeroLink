# Heltec V2 + ExpressLRS 900 MHz (Fase 1)

Esta pasta **não contém código C++ próprio** — ela contém a configuração e o passo-a-passo para flashar o **firmware oficial do ExpressLRS** na placa **Heltec WiFi LoRa 32 V2** (ESP32 + SX1276) e transformá-la no módulo de rádio 900 MHz do OpenRC AeroLink.

> 🎯 **Objetivo da Fase 1**: validar o link ELRS end-to-end usando o RX 900 MHz comprado, com o NANO_TX_v2 já enviando CRSF. A UI da OLED interna será a **nativa do ELRS** (não vamos customizar nesta fase).
>
> 🚧 **Fase 2 (TCC, futura)**: firmware próprio com 2 OLEDs, encoder, buzzer e menu customizado — esse código vai em `firmware/Heltec_V2_TX_custom/` quando chegarmos lá.

---

## 📋 Pré-requisitos

| Item | Onde conseguir |
|---|---|
| Placa **Heltec WiFi LoRa 32 V2** com **SX1276** (banda 868/915 MHz) | já adquirida |
| Cabo USB-C ou microUSB (depende da revisão da placa) | qualquer cabo de dados (não só de carga) |
| Antena 900 MHz com conector U.FL/IPEX | comprar separado se a placa não veio com uma |
| **Receptor ELRS 900 MHz** no avião | Happymodel EPW6 / BetaFPV SuperD 900 / Matek R24-D 900 |
| **ExpressLRS Configurator** | https://github.com/ExpressLRS/ExpressLRS-Configurator/releases |
| Drivers USB CP2102 (Windows) | https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers |

> ⚠️ **Confira a banda do seu RX antes de comprar.** ELRS 900 MHz (SX1276/RFM95) **não** fala com receptores 2.4 GHz (SX1280). A confusão é comum.

---

## 🔌 Conexões físicas

### Heltec V2 ↔ Arduino Nano (NANO_TX_v2)

```
┌─────────────────────────────────────────────────────────────┐
│  NANO_TX_v2                          Heltec V2              │
│  ───────────                         ──────────             │
│  TX0 (D1)  ───────────►              GPIO 17  (UART2 RX)    │
│  RX0 (D0)  ◄───────────              GPIO 23  (UART2 TX)    │
│  GND       ───────────               GND                    │
│                                                             │
│  ⚠️ NÃO conectar VCC entre placas. Cada uma com sua fonte:  │
│     - Nano: 5V do shield do controle                        │
│     - Heltec: 5V do USB ou pad VIN (até ~7V)                │
│                                                             │
│  💡 Sobre o nível lógico:                                   │
│     Nano TX0 manda 5V; Heltec GPIO17 é 5V-tolerant nominal. │
│     Em produção, recomendo divisor 3k3/1k8 (5V → ~3.3V)     │
│     para máxima segurança da longo prazo.                   │
└─────────────────────────────────────────────────────────────┘
```

### Pinout completo do Heltec V2 (referência)

| GPIO | Função | Uso no projeto |
|---|---|---|
| **Reservados pela placa** | | |
| 5  | SX1276 SCK | (interno) |
| 19 | SX1276 MISO | (interno) |
| 27 | SX1276 MOSI | (interno) |
| 18 | SX1276 NSS/CS | (interno) |
| 14 | SX1276 RST | (interno) |
| 26 | SX1276 DIO0 | (interno) |
| 35 | SX1276 DIO1 | (interno, input-only) |
| 4  | OLED SDA | (interno) |
| 15 | OLED SCL | (interno) |
| 16 | OLED RST | (interno) |
| 25 | LED branco on-board | indicador ELRS (link/status) |
| 0  | Botão PRG | bind/reset ELRS |
| **Usados na Fase 1** | | |
| 17 | UART2 RX | ← TX0 do Nano (CRSF in) |
| 23 | UART2 TX | → RX0 do Nano (telemetria, opcional) |
| **Reservados para Fase 2 (UI custom)** | | |
| 21 | I2C SDA (HW livre) | → OLED externa SDA |
| 22 | I2C SCL (HW livre) | → OLED externa SCL |
| 32 | Interrupt-capable | ← Encoder CLK |
| 33 | Interrupt-capable | ← Encoder DT |
| 36 | Input-only (ADC) | ← Encoder SW |
| 13 | GPIO livre | → Buzzer (via transistor) |

---

## 🚀 Flash do firmware ELRS — passo a passo

### 1. Instale o ExpressLRS Configurator

Baixe a versão mais recente (v1.6.x ou superior) em https://github.com/ExpressLRS/ExpressLRS-Configurator/releases para seu OS.

### 2. Abra o Configurator e selecione

- **Firmware version**: a mais recente estável (v3.5.x na data deste doc)
- **Category**: `DIY Devices`
- **Device**: `DIY 900 ESP32 SX127x RFM95 TX via UART`

### 3. Aplique o layout customizado

Esta é a etapa-chave. O target oficial usa um pinout diferente do Heltec V2.

1. Na tela do Configurator, clique em **"Custom Hardware Layout"** (ou "Custom Target Spec" em versões mais novas).
2. Cole o **conteúdo de [`heltec_v2_sx1276.json`](./heltec_v2_sx1276.json)** no campo.
   - Você pode editar o JSON no próprio Configurator antes de flashar.
3. Confirme.

### 4. Configuração de binding phrase

- **Binding Phrase**: defina uma frase única (ex: `aerolink-tcc-vb-2026`) — anote, vai precisar para o RX.
- **Regulatory Domain**: `Regulatory Domain 915` para o Brasil (ISM 902-928 MHz).
  - 868 MHz é Europa.
- **Auto Wifi On Interval**: 60 segundos é confortável.

### 5. Flash

- Conecte o Heltec V2 via USB.
- No Configurator, clique em **Build & Flash** (ou **Flash** se já tem build).
- Selecione a porta COM/serial do Heltec (driver CP2102 instalado).
- Aguarde — deve mostrar `Done` em 60-120 s.

### 6. Boot

- Após reset, o LED branco do Heltec pisca, e a OLED interna mostra a UI nativa do ELRS (versão, modo, RSSI/LQ quando linkado).
- Nos primeiros 60 s, o Heltec abre um WiFi AP chamado **`ExpressLRS TX`** com senha `expresslrs`.
  - Conecte um celular/notebook e acesse `http://10.0.0.1` para configurar parâmetros, atualizar firmware OTA, ver canais.

### 7. Bind do RX 900 MHz

1. Flashe o RX com o **mesmo binding phrase** (usa o Configurator com o RX selecionado, ou flashe via Betaflight Passthrough se o RX já está num FC).
2. Ligue o RX → ele entra em bind automaticamente nos primeiros 30 s.
3. Heltec deve mostrar `LINKED` na OLED e LED do RX deve ficar sólido.

### 8. Teste com o NANO_TX_v2

- Ligue o Nano com o firmware v2 ([`firmware/TX_NANO_v2/`](../TX_NANO_v2/README.md)).
- Confirme que o LED do Nano está piscando (loop rodando).
- Conecte TX0 do Nano → GPIO17 do Heltec + GND comum.
- No web UI do Heltec, vá em **"RC Channels"**: os canais CH1..CH7 devem se mover quando você mexer nos pots. CH8 muda em degraus conforme combinações de switches.

---

## ✅ Checklist de validação

- [ ] Heltec flashado com ELRS + layout custom (sem erro de build)
- [ ] OLED do Heltec mostra UI do ELRS (versão e "Searching..." ou "LINKED")
- [ ] WiFi AP `ExpressLRS TX` aparece na lista do celular
- [ ] Web UI do Heltec acessível em `http://10.0.0.1`
- [ ] RX 900 MHz bindado (LED sólido)
- [ ] Heltec mostra `LINKED` + RSSI / LQ
- [ ] Nano com firmware v2 piscando LED
- [ ] CH1..CH7 no web UI se movem com os pots
- [ ] CH8 no web UI muda em degraus com switches
- [ ] RX no avião responde aos canais (PWM/SBUS/CRSF, depende do RX)
- [ ] **nRF24 ainda funcionando**: NANO_RX antigo / ESP32 FC ainda recebem (sem regressão)

---

## 📚 Outros docs nesta pasta

- [`heltec_v2_sx1276.json`](./heltec_v2_sx1276.json) — layout custom pra colar no Configurator
- [`pinout_diagram.md`](./pinout_diagram.md) — diagrama detalhado da pinagem
- [`troubleshooting.md`](./troubleshooting.md) — problemas comuns e soluções

---

## 🔭 O que vem na Fase 2

A UI nativa do ELRS na OLED interna é funcional mas limitada — não tem menus do nosso HUD antigo (cards, calibração, opções de modo). Na Fase 2, vamos:

1. Escrever firmware Arduino/PlatformIO próprio para o Heltec V2
2. Implementar o **lado TX do protocolo ELRS** (mesmo sync word, FHSS, frames) para manter compatibilidade com o RX já bindado
3. Adicionar **2ª OLED externa** (I²C nos pinos 21/22) com nossos cards
4. Migrar **encoder + buzzer** do ESP8266 para o Heltec (GPIOs 32/33/36/13)
5. **Aposentar fisicamente o ESP8266**
6. Reaproveitar a lógica de menus/cards do `firmware/TX_ESP8266/TX_ESP8266.ino` (mantida no repo como referência)

A pasta dessa fase será `firmware/Heltec_V2_TX_custom/` — criada quando você der GO depois de validar a Fase 1.
