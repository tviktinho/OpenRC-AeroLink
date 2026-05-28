# Heltec V2 — Diagrama completo de pinagem (com upgrade ELRS)

```
                       Heltec WiFi LoRa 32 V2 (ESP32 + SX1276)

                  ┌──────────────────────────────────────────┐
                  │ ╔════════════════════════════════════╗ ⏛  │ ← Antena U.FL → 900 MHz
                  │ ║                                    ║    │
              ┌───┤ ║         OLED 0.96"  128x64         ║    ├───┐
   PRG  ──── │   │ ║       (SSD1306, I2C SW)            ║    │   │
   (GPIO 0)  │   │ ║                                    ║    │   │
              │   │ ╚════════════════════════════════════╝    │   │
   RST  ──── │   │                                            │   │
              │   │   ESP32 (240MHz, 4MB flash, 520KB SRAM)   │   │
              └───┤              + SX1276 (LoRa)              ├───┘
                  │                                            │
                  └──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┘
                     │  │  │  │  │  │  │  │  │  │  │  │  │  │
                     5V  3V3 G  G  ...  (40 pinos no total — abaixo)


╔═══════════════════════════════════════════════════════════════════════════╗
║                            PINAGEM (lado esquerdo)                        ║
╠═══════════════╦═══════════════════════════════╦═══════════════════════════╣
║   PINO        ║          FUNÇÃO               ║   USO NO PROJETO          ║
╠═══════════════╬═══════════════════════════════╬═══════════════════════════╣
║ 3V3           ║ Alimentação 3.3V              ║ (não usar para Nano)      ║
║ 5V            ║ Alimentação 5V (USB/VIN)      ║ (não usar para Nano)      ║
║ GND           ║ Terra                         ║ ✓ Comum c/ Nano + RX      ║
║ GPIO 36 (VP)  ║ ADC1_0, input-only            ║ Fase 2: Encoder SW        ║
║ GPIO 37       ║ ADC1_1, input-only            ║ livre                     ║
║ GPIO 38       ║ ADC1_2, input-only            ║ livre                     ║
║ GPIO 39 (VN)  ║ ADC1_3, input-only            ║ livre                     ║
║ GPIO 34       ║ ADC1_6, input-only, DIO2 LoRa ║ (reservado, SX1276)       ║
║ GPIO 35       ║ ADC1_7, input-only, DIO1 LoRa ║ (reservado, SX1276)       ║
║ GPIO 32       ║ ADC1_4, touch, PWM            ║ Fase 2: Encoder CLK       ║
║ GPIO 33       ║ ADC1_5, touch, PWM            ║ Fase 2: Encoder DT        ║
║ GPIO 25       ║ DAC1, LED BUILTIN             ║ Indicador ELRS link       ║
║ GPIO 26       ║ DAC2, DIO0 LoRa               ║ (reservado, SX1276)       ║
║ GPIO 27       ║ MOSI LoRa, PWM                ║ (reservado, SX1276)       ║
║ GPIO 14       ║ RST LoRa, PWM, SPI clock alt  ║ (reservado, SX1276)       ║
║ GPIO 12       ║ MTDI, boot-sensitive          ║ livre (cuidado boot)      ║
║ GPIO 13       ║ MTCK, PWM                     ║ Fase 2: Buzzer (NPN+R)    ║
║ GPIO 9 / 10   ║ Flash interno (NÃO USAR)      ║ ❌                        ║
║ GPIO 11       ║ Flash interno (NÃO USAR)      ║ ❌                        ║
╠═══════════════╩═══════════════════════════════╩═══════════════════════════╣
║                            PINAGEM (lado direito)                         ║
╠═══════════════╦═══════════════════════════════╦═══════════════════════════╣
║   PINO        ║          FUNÇÃO               ║   USO NO PROJETO          ║
╠═══════════════╬═══════════════════════════════╬═══════════════════════════╣
║ Vext          ║ Saída controlada 3.3V         ║ alimentar OLED externa?   ║
║ GND           ║ Terra                         ║ ✓                         ║
║ GPIO 15       ║ SCL OLED (interno)            ║ (reservado, OLED interna) ║
║ GPIO 2        ║ ADC2_2, LED (em algumas rev.) ║ livre (boot-sensitive)    ║
║ GPIO 17       ║ U2_TXD (default pin)          ║ ✓ UART2 RX REMAPEADO ¹    ║
║ GPIO 5        ║ SCK LoRa                      ║ (reservado, SX1276)       ║
║ GPIO 18       ║ NSS/CS LoRa                   ║ (reservado, SX1276)       ║
║ GPIO 19       ║ MISO LoRa, U0_CTS             ║ (reservado, SX1276)       ║
║ GPIO 21       ║ I2C SDA (default)             ║ Fase 2: OLED externa SDA  ║
║ GPIO 22       ║ I2C SCL (default)             ║ Fase 2: OLED externa SCL  ║
║ GPIO 23       ║ V_SPI MOSI alt                ║ ✓ UART2 TX REMAPEADO ¹    ║
║ GPIO 1 (TX0)  ║ Serial USB TX                 ║ debug via USB             ║
║ GPIO 3 (RX0)  ║ Serial USB RX                 ║ debug via USB             ║
║ GPIO 0        ║ Botão PRG, boot strapping     ║ Bind ELRS / boot ROM      ║
║ GPIO 16       ║ RST OLED interna              ║ (reservado, OLED interna) ║
║ GPIO 4        ║ SDA OLED interna              ║ (reservado, OLED interna) ║
╚═══════════════╩═══════════════════════════════╩═══════════════════════════╝
```

> ¹ **Sobre o remap da UART2**: por default, no ESP32 a UART2 sai em GPIO 17 (TXD) e entra em GPIO 16 (RXD). No nosso layout, **invertemos os papéis via software** (`hardware.json` + `Serial2.begin(..., RX=17, TX=23)`): GPIO 17 vira RX (recebe CRSF do Nano) e GPIO 23 vira TX (manda telemetria de volta). Fizemos isso porque o GPIO 16 default está sendo usado como saída PWM CH7 no ESP32 FC v2 — então remapear evita conflito. O ESP32 permite usar quase qualquer GPIO como UART, então a troca é segura.

## Mapa visual da Fase 1

```
                    ┌─────────────────────────────────────┐
                    │       Heltec WiFi LoRa 32 V2        │
                    │                                     │
   GPIO 17 (RX2) ◄──┼─ TX0 do Arduino Nano  (CRSF 420k)   │
   GPIO 23 (TX2) ──►┼─ RX0 do Arduino Nano  (telem opc.)  │
   GND           ───┼─ GND comum                          │
                    │                                     │
                    │   ┌──────────────┐                  │
                    │   │   SX1276     │   ⏛───► 900 MHz  │
                    │   │   (interno)  │      antena U.FL │
                    │   └──────────────┘                  │
                    │                                     │
                    │   ┌──────────────┐                  │
                    │   │  OLED 0.96"  │  UI nativa ELRS  │
                    │   │  (interno)   │                  │
                    │   └──────────────┘                  │
                    │                                     │
                    │   LED 25: pisca = procurando        │
                    │            sólido = LINKED          │
                    └─────────────────────────────────────┘
```

## Mapa visual da Fase 2 (planejada)

```
                    ┌─────────────────────────────────────┐
                    │       Heltec WiFi LoRa 32 V2        │
                    │                                     │
   GPIO 17 ◄────────┼─ NANO_TX (CRSF)                     │
   GPIO 23 ────────►┼─ NANO_TX (telem)                    │
                    │                                     │
                    │   ┌──────────────┐                  │
                    │   │   SX1276     │   ⏛───► 900 MHz  │
                    │   └──────────────┘                  │
                    │                                     │
                    │   ┌──────────────┐                  │
                    │   │  OLED int.   │  Cards: link,    │
                    │   │  (interna)   │  bateria, modo   │
                    │   └──────────────┘                  │
                    │                                     │
   GPIO 21/22 ──────┼──► ┌──────────────┐                 │
                    │    │ OLED externa │  Cards: telem,  │
                    │    │  (SSD1306)   │  alt, vel, dist │
                    │    └──────────────┘                 │
                    │                                     │
   GPIO 32/33/36 ◄──┼── Encoder (KY-040)                  │
   GPIO 13 ────────►┼── Buzzer (NPN driver)               │
                    └─────────────────────────────────────┘

   ESP8266 antigo: REMOVIDO da PCB.
   Encoder + buzzer + OLEDs todos no Heltec.
```
