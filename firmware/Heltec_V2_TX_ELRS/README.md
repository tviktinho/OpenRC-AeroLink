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
│  TX0 (D1) ──[3k3]──┬──►               GPIO 17 (UART2 RX)    │
│                   [1k8]                                     │
│                    │                                        │
│                   GND                                       │
│  RX0 (D0)  ◄───────────              GPIO 23  (UART2 TX)    │
│  GND       ───────────               GND                    │
│                                                             │
│  ⚠️ NÃO conectar VCC entre placas. Cada uma com sua fonte:  │
│     - Nano: 5V do shield do controle                        │
│     - Heltec: 5V do USB ou pad VIN (até ~7V)                │
│                                                             │
│  ⚠️ DIVISOR DE TENSÃO OBRIGATÓRIO entre TX0 do Nano e       │
│     GPIO 17 do Heltec.                                      │
│     Nano TX0 manda 5V; GPIOs do ESP32 são 3,3V e NÃO são    │
│     5V-tolerant (vide datasheet Espressif ESP32 §5.2).      │
│     Sem divisor, há risco real de dano ao chip — pode       │
│     funcionar inicialmente e morrer com uso prolongado.     │
│                                                             │
│     Opção mais robusta: módulo level shifter bidirecional   │
│     (TXS0108E, BSS138 — vende a R$ 10 no Mercado Livre).    │
│                                                             │
│     Sentido inverso (GPIO 23 → RX0): 3,3V do ESP32 é HIGH   │
│     válido p/ ATmega328P @ 5V. Esse lado dispensa shifter.  │
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

> ⚠️ **ELRS 4.x mudou o método de hardware custom.** Antes (1.x) era um campo "Custom Hardware Layout" no próprio Configurator. Agora (4.x) o hardware é definido **via web UI no primeiro boot**, arrastando o `hardware.json` deste repo.

### 1. Instale o ExpressLRS Configurator

Baixe a versão mais recente em https://github.com/ExpressLRS/ExpressLRS-Configurator/releases para seu OS.

### 2. Abra o Configurator e selecione

- **Categoria**: `DIY devices 900 MHz`
- **Aparelho**: `DIY ESP32 RFM95 900MHz TX`
- **Método de instalação**: `UART (Serial)`
- **Versões**: marque "Mostrar pré-lançamentos" e selecione `4.0.1` ou superior — é nessa série que o fluxo `/hardware.html` está consolidado

### 3. Configure as opções do aparelho

| Campo | Valor |
|---|---|
| **Domínio regulatório** | `915 MHz FCC` (Brasil/EUA) |
| **Frase de conexão personalizada** | **invente uma frase única** (ex: `aerolink-tcc-vb-2026`) — **anote**, vai precisar idêntica no RX |
| **Atraso na inicialização do Wi-Fi** | `60` segundos |
| **Apagar antes de atualizar** | **☑ marcar** (importante — garante que não tem `hardware.json` residual) |
| **Forçar atualização** | desmarcado |

### 4. Flash

- **Plugue a antena 900 MHz no IPEX1** (não pode esquecer)
- Conecte o Heltec V2 via USB ao PC
- Clique **INSTALAR**
- Aguarde 60-120 s. No fim aparece `Done!`

### 5. Configurar pinout via web UI (passo NOVO do ELRS 4.x)

Como o Heltec V2 **não é um target oficial**, o ELRS não vai achar o `hardware.json` ao bootar — e por isso vai cair direto em modo WiFi com OLED apagada ou mostrando mensagem do tipo "No HW config".

1. No celular/PC, conecte no WiFi chamado **`ExpressLRS TX`** (senha: `expresslrs`).
   - Esse WiFi fica aberto por 60s após boot. Se não aparecer, pressione RST no Heltec.
2. Abra o navegador em **`http://10.0.0.1/hardware.html`** (note o `/hardware.html`, NÃO só `/`).
3. Você vai ver uma área de **"drop target"** para arrastar um JSON.
4. **Arraste o arquivo [`hardware.json`](./hardware.json)** deste repo (a versão limpa, sem comentários) na área indicada.
   - Alternativa: clique em **"Edit"** e cole o conteúdo manualmente.
5. Clique em **"Save"** ou **"Apply"**.
6. O Heltec reboota sozinho com a configuração do Heltec V2 aplicada.

### 6. Boot com pinout correto

- Após o reboot, a **OLED interna acende** mostrando logo ELRS + versão + "Searching..."
- LED branco (GPIO 25) pisca lentamente → procurando RX
- WiFi `ExpressLRS TX` continua disponível por 60s pra configurar packet rate, potência, etc. em `http://10.0.0.1/`

### 7. Bind do RX 900 MHz

1. Flashe o RX com o **mesmo binding phrase** (usa o Configurator com o RX selecionado).
2. Coloque o RX em bind mode: ligue/desligue a alimentação **3 vezes em <2s cada vez**.
3. Heltec deve mostrar `LINKED` na OLED + LED do RX fica sólido.

### 8. Teste com o NANO_TX_v2

- Ligue o Nano com o firmware v2 ([`firmware/TX_NANO_v2/`](../TX_NANO_v2/README.md)).
- Confirme que o LED do Nano está piscando (loop a 50 Hz rodando).
- Conecte TX0 do Nano → GPIO 17 do Heltec + GND comum.
- No web UI do Heltec (`http://10.0.0.1`), vá em **"RC Channels"**: os canais CH1..CH7 devem se mover quando você mexer nos pots. CH8 muda em degraus conforme combinações dos 4 switches.

> 💡 **Por que esse fluxo é melhor que o antigo**: você pode reconfigurar pinos do Heltec a qualquer momento via web UI sem refazer o flash. Útil quando for adicionar 2ª OLED, encoder, buzzer na Fase 2 — basta arrastar um `hardware.json` atualizado.

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

- **[`hardware.json`](./hardware.json)** — **arquivo a arrastar em `/hardware.html`** (formato limpo do ELRS 4.x)
- [`heltec_v2_sx1276.json`](./heltec_v2_sx1276.json) — versão comentada do mesmo layout (documentação, NÃO usar pra upload)
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
