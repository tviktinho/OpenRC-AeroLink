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
| UART | 38400 baud, frame custom para ESP8266 HUD | **420000 baud, frame CRSF** padrão p/ ELRS |
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
│  TX0 (D1)  ────────► GPIO 17 do Heltec V2  (CRSF data out)   │
│  RX0 (D0)  ◄──────── GPIO 23 do Heltec V2  (telemetria de    │
│                                              volta, opcional)│
│  GND       ────────  GND comum                               │
│                                                              │
│  ⚠️ Não usar divisor de tensão: o Heltec é ESP32 3.3V e o    │
│     ATmega328 manda 5V no TX0. O ESP32 é 5V-TOLERANT nos     │
│     pinos de input. Se quiser sair conservador, use um       │
│     divisor 3k3/1k8 (5V -> ~3.3V).                           │
└──────────────────────────────────────────────────────────────┘
```

---

## 📡 Protocolo CRSF (emitido na TX0)

| Campo | Valor |
|---|---|
| Baud rate | **420 000** |
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

Como o frame CRSF é binário, o Serial Monitor padrão não ajuda. Duas opções:

### A) Hex dump via segundo Arduino
Use outro Nano/Mega rodando este sketch simples na Serial1 (Mega) a 420000:
```c
void setup(){ Serial.begin(115200); Serial1.begin(420000); }
void loop(){
    while (Serial1.available()) {
        uint8_t b = Serial1.read();
        Serial.print(b, HEX); Serial.print(' ');
    }
}
```
Conecte TX0 do Nano_v2 → RX1 (pino 19) do Mega + GND comum. Você verá frames `C8 18 16 ...` a cada 20 ms.

### B) Sniffer no Heltec já flashado
Se já flashou ELRS no Heltec, abra o painel web (após conectar no WiFi do Heltec) e vá em **Telemetry → CRSF** ou **RC Channels**. As barras devem se mover ao mover os sticks.

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
