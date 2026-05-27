# ESP32 FC v2 — entrada CRSF do RX ELRS 915 MHz

Firmware do **ESP32 do avião** atualizado para receber **CRSF direto do RX ELRS 915 MHz** (ELRS-915M Nano / BetaFPV ELRS Lite Nano RX 900), substituindo a entrada nRF24L01 do firmware legado.

> 🎯 **Objetivo**: fechar o ciclo end-to-end da Fase 1 — agora o link OpenRC AeroLink → Heltec → RX ELRS → ESP32 FC consegue mover servos/ESC reais.

---

## 🔗 Posição na arquitetura

```
                       (CONTROLE)
   NANO_TX v2 ──CRSF 420k──► Heltec V2 ──ELRS 915 MHz──┐
                                                       │
                                                       │  RF (LoRa)
                                                       ▼
                                              ┌──────────────────┐
                                              │  RX ELRS 915M    │
                                              │  (no avião)      │
                                              └────────┬─────────┘
                                                       │ CRSF
                                                       │ 420000 baud
                                                       ▼
                                            ┌──────────────────────┐
                                            │  ESP32 FC v2 ★ aqui  │
                                            │  (este firmware)     │
                                            │                      │
                                            │  Mixagem elevon +    │
                                            │  failsafe + PWM      │
                                            └────────┬─────────────┘
                                                     │ PWM x8
                                                     ▼
                                                Servos + ESC
```

---

## ✨ Diferenças vs firmware/ESP32/ESP32.ino legado

| Aspecto | Legado | v2 (este) |
|---|---|---|
| Entrada RC | nRF24L01 SPI | **CRSF UART2 @ 420 000 baud** |
| Pinos da entrada | CE=4, CSN=5, MOSI=23, MISO=19, SCK=18 | **GPIO 32 (RX), GPIO 33 (TX opcional)** |
| Lib RF | `RF24` | (removida) |
| Saídas PWM | CH1..CH8 nos pinos 12/13/14/27/26/25/16/17 | **idênticas** |
| Mixagem elevon | E - A, E + A, expo, diff, reflex | **idêntica** |
| Failsafe | 200 ms sem pacote | 200 ms sem frame CRSF |
| MPU6050 | inicializado mas não usado p/ estabilizar | **removido (vai voltar na Fase 2)** |
| NVS de trims | sim | **removido (vai voltar na Fase 2)** |
| Modo manual/estab | `pkt.s1 == 0` | **bit0 do CH8 (SW1 do TX)** |
| Motor cut | (não tinha) | **bit1 do CH8 (SW2)** |

> 🧹 **Decisão de design**: limpei MPU6050 e NVS deste firmware para deixar o caminho do CRSF nítido. Eles voltam na Fase 2 quando a stack de estabilização for redesenhada.

---

## 🔌 Pinagem completa

### ESP32 DevKit ↔ RX ELRS-915M

```
┌──────────────────────────────────────────────────────────────────┐
│  RX ELRS 915M Nano             ESP32 DevKit                      │
│  ──────────────────            ─────────────                     │
│  VCC  (3.6–5.5V)  ───────────► 5V (ou 3V3, ambos servem)         │
│  GND               ───────────  GND                              │
│  TX   (CRSF out)   ──────────► GPIO 32   (UART2 RX)              │
│  RX   (CRSF in)    ◄────────── GPIO 33   (UART2 TX, telemetria)  │
│  Antena T          (no IPEX1)                                    │
│                                                                  │
│  ⚠️ Se seu RX tem só 3 pinos (VCC/GND/SIG = single-wire),        │
│     ligue SIG no GPIO 32 e mude o firmware para half-duplex      │
│     (mais código — pede ajuda quando precisar).                  │
└──────────────────────────────────────────────────────────────────┘
```

### ESP32 DevKit ↔ servos / ESC

| Canal CRSF | Função | GPIO ESP32 | Equipamento típico |
|---|---|---|---|
| CH1 | Throttle | **12** | ESC (sinal) |
| CH2 | Rudder | **13** | Servo leme |
| CH3 | Aileron (vira elevon L pós-mix) | **14** | Servo asa esquerda |
| CH4 | Elevator (vira elevon R pós-mix) | **27** | Servo asa direita |
| CH5 | Aux1 | **26** | Flap / livre |
| CH6 | Aux2 | **25** | Trem de pouso / livre |
| CH7 | Aux3 | **16** | Câmera gimbal / livre |
| CH8 | Switches bitmask (não vai pra servo) | **17** | livre (saída fica em 1500 µs) |

### Pinos livres pra Fase 2

| GPIO | Uso planejado |
|---|---|
| 21 / 22 | I²C MPU6050 (mantém pinout legado) |
| 4 / 5 | livres (eram nRF24 CE/CSN) |
| 18 / 19 / 23 | livres (eram nRF24 SPI) |

---

## 💾 Compilação e flash

1. Arduino IDE 2.x ou PlatformIO.
2. Placa: **ESP32 Dev Module** (ou compatível com seu DevKit específico).
3. Biblioteca: **ESP32Servo** (Library Manager).
4. Abra `firmware/ESP32_FC_v2/ESP32.ino`. O `crsf_rx.h` está na mesma pasta e é incluído automaticamente.
5. Upload normal pela USB.

### Configuração de board (testado)
```
Board:           ESP32 Dev Module
Upload Speed:    921600
CPU Frequency:   240 MHz (WiFi/BT)
Flash Frequency: 80 MHz
Flash Size:      4MB
Partition:       Default 4MB with spiffs
```

---

## 🧪 Como testar (sem montar no avião)

### Setup mínimo de bancada
1. ESP32 alimentado via USB.
2. RX ELRS 915M alimentado pelo 5V do ESP32 (e GND comum).
3. Heltec V2 + Nano TX v2 já bindados com o RX (ver `firmware/Heltec_V2_TX_ELRS/README.md`).
4. **Sem servos plugados** ainda — só observa a Serial Monitor.

### O que esperar na Serial Monitor (115200 baud)
```
==== ESP32 FC v2 — entrada CRSF (ELRS 915) ====
CRSF iniciado em UART2: RX=GPIO32 TX=GPIO33 @420000 baud
Sistema pronto. Aguardando CRSF...
[FC] ok=50 bad=0 | thr=1500 ele=+0.00 ail=+0.00 rud=1500 | sw=0x0 STAB CUT
[FC] ok=100 bad=0 | thr=1500 ele=+0.00 ail=+0.00 rud=1500 | sw=0x0 STAB CUT
[FC] ok=1800 bad=0 | thr=1200 ele=-0.30 ail=+0.15 rud=1500 | sw=0x1 MANUAL
                ↑↑↑↑                                       ↑↑↑
                contador de frames OK              bits dos switches
```

- `ok=` aumentando rápido (~50/s) e `bad=0` → CRSF chegando perfeito.
- `ok=0` permanente → fiação invertida (RX/TX do RX trocados) ou RX não bindou.
- `bad=` aumentando → ruído elétrico (afasta cabos), nível lógico errado, baud rate diferente.
- Mexer os pots do controle → valores de `thr/ele/ail/rud` se mexem na tela.
- Apertar SW1 → muda de `STAB` para `MANUAL`.
- Apertar SW2 → aparece `CUT` (motor cortado).

### Quando ligar nos servos
- Só depois de **confirmar mensagens da Serial corretas**.
- Use **fonte separada** para os servos (não puxar do 5V do ESP32 que é fraco — usar BEC dedicado).
- GND da fonte dos servos **TEM** que ser comum com o GND do ESP32.

---

## 🛡 Failsafe

| Condição | Tempo | Ação |
|---|---|---|
| Sem frame CRSF válido | 200 ms | Motor → 1000 µs (idle), servos → 1500 µs (neutro), elevons → 1500+reflex |
| CRC8 inválido | (não trava) | Frame descartado silenciosamente, contador `bad` incrementa |
| RX desligado/avião fora de alcance | depende do ELRS | RX para de mandar CRSF → cai no failsafe acima |

> 💡 O ELRS tem failsafe próprio dentro do RX (configurável pelo web UI do Heltec). Você pode setar valores específicos por canal lá. O failsafe deste firmware é o "segundo nível" — se mesmo o failsafe do RX falhar, ainda assim os servos vão pra posição segura.

---

## 🔧 Troubleshooting

### Serial mostra `ok=0` e `bad=0`
- Nenhum byte chegando na UART2. Cheque:
  - Fio do TX do RX ligado no GPIO 32 do ESP32 (e não outro pino).
  - GND comum entre RX e ESP32.
  - RX está alimentado (LED do RX acende?).
  - RX já bindou com o Heltec (LED sólido).

### Serial mostra `ok` aumentando, mas valores não mudam quando mexo o stick
- Verifique no painel web do Heltec se os canais estão se mexendo. Se não, problema está antes do RX (no Nano ou Heltec).
- Se no painel web mexe mas no FC não, confira que está usando o **mesmo binding phrase** dos dois lados (TX e RX flashados com a mesma frase).

### Serial mostra `bad=` aumentando rápido junto com `ok`
- Ruído elétrico no cabo CRSF. Soluções:
  - Reduzir comprimento do fio (idealmente <10 cm).
  - Adicionar GND junto do sinal (par trançado).
  - Aterrar a antena do RX (com cuidado pra não tocar o vivo).

### Servos não se mexem
- Confira que estão alimentados por fonte externa (BEC), não pelo USB do ESP32.
- GND da fonte do servo TEM que ser comum com GND do ESP32.
- Servos digitais querem 6V; analógicos aceitam 4.8-6V. ESC manda o BEC dele, geralmente 5V.

### ESC arma e bipa, mas motor não gira
- Throttle no centro está em 1500 µs. ESC quer 1000 µs pra armar (no extremo baixo).
- Se você ligou tudo com o stick no meio, o ESC pode estar em failsafe próprio.
- Throttle baixo no TX → libera motor.

---

## 🔭 Próximos passos (Fase 2)

- Reintegrar **MPU6050** + AHRS (filtro complementar) — base já no `firmware/ESP32/ESP32.ino` legado, é só portar.
- Modo **STAB de verdade**: PID nos sticks usando taxa do gyro como entrada.
- Telemetria CRSF de volta (RSSI, LQ, voltagem da bateria do avião) — usa o GPIO 33 (TX) que já está cabeado.
- NVS para trims persistentes.
- Suporte ao **RX single-wire** (alguns clones de ELRS Nano).
