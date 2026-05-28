# CRSF Sniffer — ESP32-C3 mini

Firmware **minimalista** pra validar o link ELRS sem precisar do FC v2 completo.

> 🎯 **Caso de uso**: você tem um RX ELRS 915M bindado com seu TX, mas não sabe se os canais estão chegando. Pluga o RX neste sniffer e vê em tempo real:
> - `[LINKED]` ou `[NO LINK]`
> - Contadores `ok=N bad=N`
> - Barras visuais dos 4 primeiros canais conforme você mexe os pots

## 🔌 Pinout

```
┌──────────────────────────────────────────────┐
│  ELRS 915M RX            ESP32-C3 mini       │
│  ───────────             ──────────          │
│  V  (5V)  ─────────────► 5V                  │
│  G        ─────────────  GND                 │
│  T  (TX)  ─────────────► GPIO 2 (UART1 RX)   │
│  R  (RX)  ◄────────────  GPIO 3 (UART1 TX)   │ ← opcional, telemetria
└──────────────────────────────────────────────┘
```

> ⚠️ **NÃO** usar GPIO 18/19 (USB nativo do ESP32-C3).
> ⚠️ **NÃO** usar GPIO 8 (strapping de boot).
> ✅ Conexão direta, sem divisor de tensão (ambos lados são 3.3V nativo).

## 🛠 Compilação e upload

### Via PlatformIO (recomendado)

```powershell
cd firmware/ESP32_C3_CRSF_SNIFFER
pio run --target upload
pio device monitor
```

### Via Arduino IDE

1. Board: **ESP32C3 Dev Module**
2. USB CDC On Boot: **Enabled**
3. Upload Speed: 921600
4. Plug ESP32-C3 mini → Upload
5. Serial Monitor a 115200 baud

## 📺 O que esperar na Serial

```
================================================
  CRSF Sniffer — ESP32-C3 mini
  OpenRC AeroLink, validação ELRS Fase 1
================================================
CRSF UART1: RX=GPIO2 TX=GPIO3 @ 400000 baud
Aguardando CRSF do RX 915M...
Mexa os pots do controle pra ver canais mudando.

[NO LINK] ok=0 bad=0 | CH1=  0us [..........] CH2=  0us [..........] CH3=  0us [..........] CH4=  0us [..........]
[NO LINK] ok=0 bad=0 | ...
                                                    ↑ LED do RX piscando lento

>>> LINK ESTABELECIDO — CRSF chegando! <<<

[LINKED] ok=50 bad=0 | CH1=1500us [#####.....] CH2=1500us [#####.....] CH3=1000us [..........] CH4=1500us [#####.....]
                                                    ↑ LED do RX vira sólido

(você mexe o pot A0 = throttle pra cima)

[LINKED] ok=100 bad=0 | CH1=1980us [#########.] CH2=1500us [#####.....] CH3=1000us [..........] CH4=1500us [#####.....]
                              ↑↑↑↑                                ↑↑↑↑↑↑↑↑↑↑↑
                       valor mudou             barra cheia conforme stick sobe
```

## 🩺 Diagnóstico

| Sintoma | Significado |
|---|---|
| `[NO LINK] ok=0 bad=0` permanente | Bind RF não fechou OU cabeamento RX↔ESP32-C3 errado |
| `[NO LINK] ok=0 bad=N` (bad crescendo) | Bytes chegam mas CRC errado — baud rate mismatch (confirma RX em 400000) |
| `[LINKED] ok=N bad=0` mas valores fixos em 1500/1000 | CRSF OK mas RX em failsafe (LED do RX deve estar piscando lento) |
| `[LINKED] ok=N bad=0` valores mudam ao mexer pots | ✅ **TUDO OK** — bind RF + CRSF funcionais |

## 🔄 Como migrar pro FC v2 depois

Quando o sniffer mostrar `[LINKED]` + canais mudando:

1. Sabe que o RX está OK
2. Desliga o ESP32-C3
3. Pluga o RX (mesmos 4 fios T/R/V/G) no **ESP32 DevKit** (FC v2)
4. Liga o FC v2 — deve replicar o mesmo comportamento, agora com PWM nos servos

Se no FC v2 não funcionar mas no sniffer funciona, **problema é no FC v2** (cabeamento, firmware, alimentação) — não no RX.

## 📚 Referências

- `firmware/ESP32_FC_v2/crsf_rx.h` — parser CRSF reutilizado (mesmo header)
- `firmware/Heltec_V2_TX_ELRS/` — config do TX
