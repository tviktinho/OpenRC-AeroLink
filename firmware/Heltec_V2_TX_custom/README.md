# Heltec V2 TX — Protocolo LoRa Customizado (Fase 2)

Substitui o ExpressLRS oficial por um **protocolo próprio** sobre LoRa raw. Sem bugs de pré-release, sem max-power hardcoded, sem dependência de Custom Hardware Layout. **Você controla TUDO**.

> 🎯 **Objetivo da Fase 2**: implementar protocolo de RC do zero usando SX1276 direto. Aprendizado profundo de RF, frame design, e LoRa pra TCC.

---

## 🏗 Arquitetura

```
                 ┌─────────────────┐
                 │  Nano TX v2     │  (já feito, não muda)
                 └────────┬────────┘
                          │ CRSF 400000 baud
                          │ (UART, divisor 3k3/1k8)
                          ▼
   ┌──────────────────────────────────┐
   │  Heltec V2 + ESTE firmware       │
   │  ──────────────────────────────  │
   │  • Recebe CRSF (reusa crsf_rx.h) │
   │  • Empacota 16 canais 11-bit     │
   │  • TX via SX1276 a 915 MHz       │
   │  • 100 mW REAIS, 50 Hz           │
   └────────────────┬─────────────────┘
                    │ LoRa raw 915 MHz
                    │ (protocolo lora_link.h)
                    ▼
              ╔═══════════════════╗
              ║  RX customizado   ║  ← Fase B (próximo passo)
              ║  (firmware no     ║
              ║   ESP8285 do      ║
              ║   ELRS-915M)      ║
              ╚═══════════════════╝
```

## 📡 Protocolo lora_link

Frame de 28 bytes, formato definido em [`lora_link.h`](./lora_link.h):

```
┌──────┬──────┬─────┬─────┬─────────────────────┬─────────┐
│ STX1 │ STX2 │ LEN │ SEQ │ PAYLOAD (22 bytes) │ CRC16   │
│ 0xC4 │ 0x71 │ 25  │ ++  │ 16 canais 11-bit   │ 2 bytes │
└──────┴──────┴─────┴─────┴─────────────────────┴─────────┘
```

**Payload é idêntico ao do CRSF_FRAMETYPE_RC_CHANNELS_PACKED** — facilita reuso de código. Quem decodifica o payload pode usar `crsf_unpack_channels()` direto.

**CRC**: CRC-16-CCITT (poly 0x1021, init 0xFFFF) sobre [SEQ + PAYLOAD]. LoRa também tem CRC interno embutido — defesa em camadas.

## ⚙️ Parâmetros LoRa

| Parâmetro | Valor | Por quê |
|---|---|---|
| Frequência | **915 MHz** | FCC US/BR, mesma do nosso projeto |
| Bandwidth | **500 kHz** | Taxa alta o suficiente pra 50 Hz |
| Spreading Factor | **7** | Chirp rápido, mas decent range |
| Coding Rate | **4/5** | Correção mínima, frame mais curto |
| Sync Word | **0x12** | Padrão LoRa (não LoRaWAN) |
| TX Power | **20 dBm = 100 mW** | Máximo do SX1276 c/ PA_BOOST |

Taxa efetiva ≈ 21.875 kbps. Time-on-air de 28 bytes ≈ 13 ms. Margem confortável pra 50 Hz (20 ms de período).

## 🔌 Ligações físicas (idênticas ao ELRS)

```
┌─────────────────────────────────────────────────────────────────┐
│  Heltec V2                       Nano TX v2                     │
│  ─────────                       ──────────                     │
│  GPIO 17 (UART2 RX) ◄──[3k3]──── TX0/D1                         │
│                       └──[1k8]── GND  (divisor 5V → 3.3V)       │
│  GPIO 23 (UART2 TX) ──────────► RX0/D0  (telemetria, opcional)  │
│  GND                ───────────  GND comum                      │
│                                                                 │
│  Antena 915 MHz no IPEX1 do Heltec (OBRIGATÓRIO antes de TX!)  │
└─────────────────────────────────────────────────────────────────┘
```

## 🛠 Compilação e upload

```powershell
cd firmware/Heltec_V2_TX_custom
pio run -t upload
pio device monitor
```

Dependências:
- `sandeepmistry/LoRa @ ^0.8.0` (instalado automaticamente pelo PlatformIO)

## 📺 O que esperar na Serial USB

```
===================================
  Heltec V2 TX — Protocolo Custom
  OpenRC AeroLink Fase 2
===================================
CRSF UART2: RX=GPIO17 TX=GPIO23 @ 400000 baud
LoRa init em 915000000 Hz... OK
LoRa configurado: SF7 BW500000 CR4/5 PWR=20 dBm SYNC=0x12
Frame: 28 bytes @ 50 Hz = 1400 bytes/s
Aguardando CRSF do Nano TX...

[TX] tx=50 | CRSF ok=50 bad=0 LIVE | CH1=1500 CH2=1500 CH3=992 CH4=992
[TX] tx=100 | CRSF ok=100 bad=0 LIVE | CH1=1500 CH2=1500 CH3=992 CH4=992
                                                          ↑↑↑↑
                                          valores reais dos canais CRSF
```

- `tx=N crescendo` → frames LoRa sendo enviados (50/s = 50 Hz)
- `LIVE` → CRSF está chegando do Nano
- `DEAD` → sem CRSF (Nano não conectado ou desligado)
- Quando você mexe um pot, os valores `CH1..CH4` mudam

## ⚠️ Limitações da v1

- **Sem RX correspondente ainda** — TX-only por enquanto. Quando você tiver o RX customizado (Fase B), aí valida end-to-end.
- **Sem FHSS** — canal fixo em 915 MHz. Vulnerável a interferência localizada. FHSS pode vir depois.
- **Sem failsafe RF do lado RX** — depende da implementação no RX customizado.
- **Sem telemetria de volta** — só TX→RX por enquanto. Bidirecional vem depois.
- **Sem auto-bind** — não há binding phrase. Hoje TX e RX vão se ouvir SEMPRE que estiverem na mesma freq+SF+BW. Se quiser proteção, dá pra adicionar UID no header do frame.

## 🧪 Como validar AGORA (TX only)

Sem RX customizado pronto, dá pra validar se o TX está mesmo transmitindo:

### Opção A — Sniffer SDR (RTL-SDR ~R$80)
1. RTL-SDR + GQRX/SDR# no PC
2. Centraliza em 915 MHz com BW 500 kHz
3. Quando o Heltec transmite, vê uma "explosão" no waterfall a cada 20 ms

### Opção B — Outro Heltec V2 / módulo SX1276 no PC
1. Roda um sniffer LoRa simples no Arduino IDE (exemplo da lib `LoRa`)
2. Configura mesmos parâmetros (915 MHz, SF7, BW500, CR4/5, SYNC 0x12)
3. Vê os bytes chegando

### Opção C — Aguardar Fase B (RX customizado)
Aí valida automaticamente — RX recebe, decodifica, cospe CRSF pro ESP32 FC v2, e tudo flui.

## 🔭 Próximos passos (Fase B do roadmap)

1. **firmware/ELRS_915M_RX_custom/**: firmware Arduino pro ESP8285 do RX
   - Setup do SX1276 com mesmos parâmetros
   - State machine pra parsear frames lora_link
   - Saída CRSF na UART pro FC v2 (mesmo formato CRSF do TX original)
2. **FTDI**: precisa de adapter USB-Serial pra flashar o ESP8285
3. **Pinout do ESP8285 do RX**: identificar TX/RX/GPIO0/GND pra programação
4. **Testar end-to-end**

## 📚 Referências

- LoRa lib by Sandeep Mistry: https://github.com/sandeepmistry/arduino-LoRa
- SX1276 datasheet: https://semtech.com/products/wireless-rf/lora-connect/sx1276
- Heltec V2 pinout: https://www.espboards.dev/esp32/heltec-wifi-lora-32-v2/

---

## 🤝 Diferenças vs `firmware/Heltec_V2_TX_ELRS/`

| Aspecto | TX_ELRS (Fase 1) | TX_custom (esta) |
|---|---|---|
| Firmware base | ExpressLRS 4.0.1 oficial | Arduino próprio |
| Configuração | hardware.json runtime | Hardcode no .ino |
| Potência | Limitada a 1 mW (bug) | **100 mW REAIS** |
| Protocolo | ELRS (FHSS, telemetria, etc.) | Próprio (simples, canal fixo) |
| Custom Hardware | Via `/hardware.html` | Sem necessidade |
| Compatível com RX comprado | Só RX ELRS oficial | Precisa RX customizado |
| Atualização | Via Configurator | Via PlatformIO |
| Aprendizado | Configuração | Implementação completa |

Os dois firmwares coexistem no repo. Você escolhe qual flashar dependendo do hardware/objetivo.
