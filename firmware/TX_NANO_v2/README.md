# TX_NANO_v2 — Transmissor com saída dupla nRF24 + CRSF

Firmware do **Arduino Nano** do controle, na versão **2.0** que prepara o upgrade para **ELRS**.

> 🔗 **Posição na arquitetura**: este Nano é o "front-end" físico do controle (lê pots, switches). Ele emite os comandos por **duas vias simultâneas**:
> 1. **nRF24L01** (compatibilidade com receptores antigos — NANO_RX, ESP32 FC, 14-BRIZA, MEGA_RX)
> 2. **UART CRSF** (alimenta o módulo Heltec V2 que faz o link ELRS 900 MHz)

---

## ✨ O que mudou em relação ao v1

| Aspecto | v1 (`firmware/legacy/NANO_TX_v1_nrf24.ino`) | v2 (este) |
|---|---|---|
| Saída RF | nRF24 apenas | nRF24 **+** UART CRSF paralelo |
| UART | 38400 baud, frame custom para ESP8266 HUD | **400000 baud, frame CRSF** padrão p/ ELRS ¹ |
| ESP8266 HUD | Receptor da UART | **Aposentado** (Heltec assume o HUD) |
| Encoder/CAL/buzzer | Lidos pelo Nano | **Migram pro Heltec** (Fase 2 — não lidos aqui) |
| Calibração min/max pots | Sim | Removida (ELRS faz calibração própria via web UI) |
| Taxa | 100 Hz | 50 Hz (combina com ELRS 900 50/100/200 Hz) |
| Switches | 2 (SW1, SW2) | 4 (SW1..SW4), agregados em CH8 |

> ✅ **Compatibilidade nRF24**: 100% mantida. Endereço `"00001"`, canal 76, 250 kbps, struct `{p[8], s1, s2}` idêntica.  
> ⚠️ Apenas SW1/SW2 vão pra struct nRF24 (legado), SW3/SW4 só vão pro CRSF (CH8 bitmask).

---

## 🔌 Pinagem

```
┌──────────────────────────────────────────────────────────────┐
│                       ARDUINO NANO                           │
├──────────────────────────────────────────────────────────────┤
│  nRF24L01            │  Potenciômetros    │  Switches        │
│  ─────────           │  ──────────────    │  ─────────       │
│  CE    → D8          │  POT0 → A0         │  SW1 → D2        │
│  CSN   → D7          │  POT1 → A1         │  SW2 → D3        │
│  MOSI  → D11 (HW)    │  POT2 → A2         │  SW3 → D4        │
│  MISO  → D12 (HW)    │  POT3 → A3         │  SW4 → D5        │
│  SCK   → D13 (HW)    │  POT4 → A4         │  (INPUT_PULLUP)  │
│  VCC   → 3.3V        │  POT5 → A5         │                  │
│  GND   → GND         │  POT6 → A6         │                  │
├──────────────────────────────────────────────────────────────┤
│  UART CRSF para HELTEC V2                                    │
│  ────────────────────────                                    │
│  TX0 (D1)  ──[3k3]──┬──► GPIO 17 do Heltec V2  (CRSF in)     │
│                    [1k8]                                     │
│                     │                                        │
│                    GND                                       │
│  RX0 (D0)  ◄──────── GPIO 23 do Heltec V2  (telem, opcional) │
│  GND       ────────  GND comum                               │
│                                                              │
│  ⚠️ DIVISOR DE TENSÃO OBRIGATÓRIO no TX0 → GPIO 17.          │
│     O Nano manda 5V; GPIOs do ESP32 são 3,3V (NÃO são        │
│     5V-tolerant — datasheet Espressif). Divisor 3k3 / 1k8    │
│     converte 5V → ~3,3V. Sem ele, risco real de dano ao      │
│     chip do Heltec (especialmente em uso prolongado).        │
│                                                              │
│     Alternativa mais robusta: level shifter bidirecional     │
│     tipo TXS0108E ou BSS138 (módulo "4-channel logic level   │
│     converter" — vende a R$ 10 no Mercado Livre).            │
│                                                              │
│     Sentido inverso (Heltec GPIO 23 → Nano RX0): 3,3V do     │
│     ESP32 é HIGH válido p/ o Nano @ 5V (Vih = 3,0V). Esse    │
│     lado não precisa de level shifter.                       │
└──────────────────────────────────────────────────────────────┘
```

---

## 📡 Protocolo CRSF (emitido na TX0)

| Campo | Valor |
|---|---|
| Baud rate | **400 000** ¹ |
| Quadro | `[0xC8] [0x18] [0x16] [22 bytes payload] [CRC8]` |
| Tipo de frame | `CRSF_FRAMETYPE_RC_CHANNELS_PACKED` (0x16) |
| Canais | 16 × 11 bit (172..1811) |
| Polinômio CRC | 0xD5 |
| Taxa de envio | 50 Hz (20 ms) |

### Mapa de canais

| Canal CRSF | Origem | Faixa |
|---|---|---|
| CH1 | Pot A0 | 0..255 → 172..1811 |
| CH2 | Pot A1 | 0..255 → 172..1811 |
| CH3 | Pot A2 | 0..255 → 172..1811 |
| CH4 | Pot A3 | 0..255 → 172..1811 |
| CH5 | Pot A4 | 0..255 → 172..1811 |
| CH6 | Pot A5 | 0..255 → 172..1811 |
| CH7 | Pot A6 | 0..255 → 172..1811 |
| **CH8** | `SW1\|SW2«1\|SW3«2\|SW4«3` (bitmask 0..15) | 16 níveis (172, 281, ..., 1811) |
| CH9..CH16 | Reservado (992 = centro) | — |

> 💡 No ExpressLRS web UI (após bind), você consegue ver os 16 canais chegando. CH1..CH8 vão se mover; CH9..CH16 ficam centrados.

> ¹ **Por que 400 000 e não 420 000 (CRSF padrão)**: o ATmega328P @ 16 MHz não gera 420 000 baud com precisão (cairia em 400 000 real com 4,76 % de erro). Usar 400 000 dá 0 % de erro. ELRS v3+ tem auto-detecção de baud rate; se o seu setup não detectar, configure manualmente em `http://10.0.0.1/hardware.html` → "Serial Baud Rate" = 400000.

---

## 🛠 Como compilar e flashar

1. Abra `firmware/TX_NANO_v2/NANO_TX.ino` no **Arduino IDE 2.x** (ou PlatformIO).
2. Placa: **Arduino Nano**. Processador: **ATmega328P** (Old Bootloader se for clone CH340).
3. Instale a lib **RF24** (TMRh20) via Library Manager.
4. **Importante**: o arquivo `crsf_tx.h` precisa estar na mesma pasta do `.ino` (o IDE já inclui automaticamente).
5. Antes de upload, **desconecte fisicamente o TX0 do Heltec** ou desligue o Heltec — o conversor USB-Serial do Nano usa a mesma UART.
6. Upload normal pelo USB.

---

## 🧪 Como testar sem o Heltec ligado

Como o frame CRSF é binário, o Serial Monitor padrão não ajuda. Três opções:

### A) Sniffer em ESP32 (recomendado)
Use um ESP32 sobrando (qualquer DevKit) com este sketch — o ESP32 gera 400 000 baud limpo, então decodifica sem erro:
```c
HardwareSerial CRSF(2);  // UART2
void setup() {
    Serial.begin(115200);
    CRSF.begin(400000, SERIAL_8N1, /*RX=*/16, /*TX=*/17);
}
void loop() {
    while (CRSF.available()) {
        uint8_t b = CRSF.read();
        Serial.printf("%02X ", b);
    }
}
```
Conecte TX0 do Nano_v2 → GPIO 16 do ESP32 + GND comum.
Você verá frames `C8 18 16 ...` a cada 20 ms.

### B) USB-serial direto no PC
Use um adaptador FTDI/CP2102 ligado em modo "snooping" + um terminal que aceite 400 000 baud (PuTTY, Tera Term, `cu` no Linux). A maioria dos chips USB-serial modernos gera 400 000 limpo.

### C) Sniffer no Heltec já flashado
Se já flashou ELRS no Heltec, abra o painel web (após conectar no WiFi do Heltec) e vá em **Telemetry → CRSF** ou **RC Channels**. As barras devem se mover ao mover os sticks. É o teste end-to-end mais fácil.

> ⚠️ **NÃO recomendado**: usar outro AVR (Nano/Mega) como sniffer. AVR @ 16 MHz não gera 400 000 limpo em todos os pinos UART (depende do RX usar mesmo divisor U2X que o TX), e Mega 2560 tem `Serial1/2/3` em 400 000 só com `U2X1=1`, que o `Serial.begin()` padrão pode não habilitar — leitura vem corrompida.

### Verificação rápida no Nano sozinho
- LED do Nano pisca a cada ~500 ms enquanto o loop estiver rodando.
- nRF24 continua emitindo — os RX antigos (NANO_RX, ESP32 FC) **continuam funcionando** sem mudança nenhuma.

---

## 🔄 Reverter para o v1 (legacy)

Se algo der errado e você precisar voltar ao firmware antigo:
1. Abra `firmware/legacy/NANO_TX_v1_nrf24.ino`
2. Flash no mesmo Nano
3. Reabra a UART do ESP8266 HUD (38400 baud, frame `AA 55 ...`)
4. Pronto, sistema antigo de volta.

---

## 📌 Próximos passos

- **Heltec V2 (Fase 1)**: ver [`firmware/Heltec_V2_TX_ELRS/README.md`](../Heltec_V2_TX_ELRS/README.md) para flashar ELRS oficial com layout custom.
- **Receptor ELRS 900 MHz**: comprar (sugestões: Happymodel EPW6, BetaFPV SuperD 900, Matek R24-D 900).
- **Fase 2 (TCC)**: firmware customizado no Heltec com 2 OLEDs, encoder, buzzer, menu próprio — substitui o ESP8266 inteiro.
