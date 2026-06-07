# NANO_TX v3 — OpenRC AeroLink

Transmissor DIY baseado em **Arduino Nano (ATmega328P @ 16 MHz)** + **nRF24L01 PA+LNA**
com **trims digitais** via placa de expansão **PCF8574** (I²C).

## O que mudou em relação ao v2

| Aspecto                    | v2                                       | v3                                                        |
|----------------------------|------------------------------------------|-----------------------------------------------------------|
| Trims                      | Inexistentes                             | 8 botões digitais via PCF8574 (4 eixos × +/−)             |
| Persistência               | Calibração em RAM (perdida no reset)     | Calibração **e** offsets de trim em EEPROM                |
| AUX                        | Slot p[6] era um pot extra (desativado)  | Slot p[6] = **botão AUX binário** em **D2**               |
| Estrutura do código        | `.ino` monolítico (`src_dir=.`)          | `src/*.cpp` modular (config / storage / inputs / trims / radio / crsf) |
| Saída nRF24                | Idêntica                                 | Idêntica                                                  |
| Saída CRSF                 | Idêntica                                 | Idêntica                                                  |

Os receptores existentes (`ESP32C3_RX_SBUS`, `ESP32_RX`, etc.) **não precisam
mudar** — o protocolo do PacketRF é compatível 1:1.

## Pinagem

```
+============================================================================+
|                        ARDUINO NANO (ATmega328P)                            |
+============================================================================+

+----------------------+        +-------------------+
|       NANO           |        |    nRF24L01+      |
|                      |        |     (PA+LNA)      |
|             VCC 3.3V +------> + VCC               |
|             GND      +------> + GND  +cap 10-100uF entre VCC e GND
|             D8 (CE)  +------> + CE              (obrigatorio!)
|             D7 (CSN) +------> + CSN
|             D13 (SCK)+------> + SCK
|             D11 (MOSI)+----->+ MOSI
|             D12 (MISO)+<-----+ MISO
|                      |        +-------------------+
|                      |
|   A0 <-- Gimbal THR  |        +-------------------+
|   A1 <-- Gimbal YAW  |        |     PCF8574       |
|   A2 <-- Gimbal PIT  |        |   (addr 0x20)     |
|   A3 <-- Gimbal ROL  |        |  A0,A1,A2 -> GND  |
|                      |        |  VCC <-- 5V       |
|   A4 (SDA) <------------------> SDA
|   A5 (SCL) <------------------> SCL
|                      |        |  (INT nao usado)  |
|   D5  <-- SW1 -- GND |        |                   |
|   D4  <-- SW2 -- GND |        | P0 --[Thr+] -- GND
|   D6  <-- CALIB- GND |        | P1 --[Thr-] -- GND
|   D2  <-- AUX  -- GND        | P2 --[Roll+]-- GND
|                      |        | P3 --[Roll-]-- GND
|   D0 (RX) -- nada    |        | P4 --[Pit+] -- GND
|   D1 (TX) ---> CRSF  |        | P5 --[Pit-] -- GND
|                      |        | P6 --[Yaw+] -- GND
|   A6, A7: LIVRES     |        | P7 --[Yaw-] -- GND
|   (analog-only —     |        +-------------------+
|    sem digital/pullup) |
+----------------------+

Observacoes:
  * D9, D10: livres. D10 fica em OUTPUT por dependencia do SPI HW.
  * D3 (INT1): livre, reservado pra expansao futura.
  * SDA/SCL precisam de pull-ups (4.7-10 kOhm) — a maioria dos breakouts
    de PCF8574 ja traz. Confira com multimetro: cada linha deve subir pra
    VCC quando solta.
  * Botoes do PCF: fechar pino do PCF contra GND. Sem resistor externo.
```

## Mapeamento final dos canais

| Slot PacketRF | CRSF CH | Origem hardware            | Conteúdo / faixa             |
|---------------|---------|----------------------------|------------------------------|
| `p[0]`        | CH1     | A0 + `trimOffset[0]`       | Throttle 0..255              |
| `p[1]`        | CH2     | A1 + `trimOffset[1]`       | Yaw      0..255              |
| `p[2]`        | CH3     | A2 + `trimOffset[2]`       | Pitch    0..255              |
| `p[3]`        | CH4     | A3 + `trimOffset[3]`       | Roll     0..255              |
| `p[4]`        | CH5     | SW1 (D5)                   | 0 / 255                      |
| `p[5]`        | CH6     | SW2 (D4)                   | 0 / 255                      |
| `p[6]`        | CH7     | **AUX (D2)** (NOVO)        | 0 / 255                      |
| `p[7]`        | CH8     | reservado                  | 128 (neutro)                 |
| `s1`          | CH9     | reservado                  | 128 (neutro)                 |
| `s2`          | CH10    | reservado                  | 128 (neutro)                 |
| —             | CH11..CH16 | só CRSF                | `CRSF_CHANNEL_VALUE_MID`     |

> Trim aplicado: `saida = constrain(byteCalibrado + trimOffset, 0, 255)`,
> com `trimOffset[i] ∈ [-15, +15]` (faixa `TRIM_MAX_ABS` em `config.h`).

## Calibração de gimbal

1. **Segura** o botão D6 (CALIB).
2. Move **todos** os gimbals nos extremos (cima/baixo/esquerda/direita) por
   alguns segundos. O firmware vai memorizando min e max de cada eixo.
3. **Volta** os sticks pro centro físico.
4. **Solta** o D6. Nesse instante o firmware:
   - Captura o valor atual como **centro** de cada eixo;
   - Grava o bloco inteiro (calib + trims) na EEPROM (1 write único);
   - Volta a transmitir (durante a calibração o TX fica MUDO — failsafe).

A calibração persiste após reset/power-cycle. Pra recalibrar, basta repetir o
gesto.

## Trims digitais

- Cada toque de um botão do PCF: incrementa/decrementa o `trimOffset` do eixo
  em **±1** byte.
- Segurar por **600 ms** começa **auto-repeat a 4 Hz** (250 ms entre pulsos).
  Sweep completo da faixa ±15 ≈ 4 s segurando.
- Soltar zera o timer; tocar de novo recomeça do passo único.

### Persistência

- Cada incremento marca a EEPROM como **dirty**, MAS o write real só acontece
  depois de **2 segundos sem nova mudança** (`EE_COMMIT_QUIET_MS`). Resultado:
  uma sessão inteira de ajuste vira **1 write** ao final, em vez de N writes
  por toque. Vida útil prática da EEPROM ≈ eterna.

### Sem gesto de reset

Decisão travada: para zerar os offsets, apague a EEPROM (sketch separado:
`for (int i=0;i<EEPROM.length();i++) EEPROM.update(i,0xFF);`) ou re-flash o
firmware após bumpar `EE_VERSION` em `config.h`.

## Como compilar e gravar

PlatformIO (extensão do VSCode):

```bash
# Clone CH340 / old bootloader (mais comum em BR)
pio run -e nano_old_bootloader
pio run -e nano_old_bootloader -t upload

# Nano original / new bootloader (raro)
pio run -e nano_new_bootloader
pio run -e nano_new_bootloader -t upload
```

### Antes do upload

- **Desconecte o fio de TX0/D1 → módulo CRSF.** A HW Serial é compartilhada
  com o bootloader USB. Se houver fio puxando, o avrdude não fala.
- Mantenha o nRF24 alimentado em 3.3V (não tente em 5V — queima).

### Primeira gravação numa placa nova

A EEPROM vem virgem (0xFF). No primeiro boot o firmware detecta isso (MAGIC ≠
0xA5) e grava defaults (calibração faixa cheia, trims em zero). O LED do
nRF24 e os dois switches devem responder imediatamente; gimbals podem precisar
de calibração (gesto D6) pra ficarem centrados corretamente.

## Scanner I²C no boot

O `setup()` faz um varredura I²C de 0x08 a 0x77 e imprime os endereços que
respondem **no Serial USB padrão (115200 baud)**. Depois, o Serial é
reconfigurado pro baud do CRSF (400 000).

Para ver:

```bash
pio device monitor -b 115200
```

Abra o monitor, aperte o **reset** da Nano, e você vê algo como:

```
[boot] NANO_TX_v3 0.1.0
[I2C scan]
  device @ 0x20
[PCF8574] OK @ 0x20
[storage] OK
[boot] iniciando CRSF + nRF24...
```

(Depois disso o Serial vai pra 400 000 baud e qualquer caractere visualizado
é "lixo" do CRSF — esperado.)

Se o scanner mostrar `device @ 0x38` em vez de `0x20`, o seu chip é
**PCF8574A** (com "A"), não o PCF8574 "normal". Edite `config.h`:

```c
#define PCF_ADDR  0x38
```

Se o scanner mostrar `(nenhum dispositivo respondeu)`, possíveis causas:
- SDA/SCL trocados;
- Sem pull-ups no barramento;
- VCC do PCF não conectado;
- Endereçamento A0/A1/A2 errado (precisa estar tudo em GND pra 0x20/0x38).

## Troubleshooting

**Trims não respondem**
- Confira `[PCF8574] OK @ ...` no boot.
- Apertando um botão (ex.: Thr+), o bit correspondente do byte lido vai pra 0.
  Pra testar sem osciloscópio, um sketch I²C que imprima o byte funciona.
- O PCF tem pull-ups internos **fracos** (~100 µA). Cabos longos (>50 cm) ou
  ambiente ruidoso podem exigir pull-ups externos de 10 kΩ por pino.

**RX em failsafe contínuo**
- O TX para de transmitir durante a calibração (botão D6 segurado). Solte o D6.
- Confira que `radio_setup()` reportou OK — sem nRF24 detectado, `s_ready`
  fica `false` e o `radio_send_packet()` vira no-op.

**CRSF não chega no Heltec/ELRS**
- Baud é **400 000** (não 420 000). Configure o módulo pra detect auto ou
  fixe em 400 000. Ver explicação técnica em `crsf_tx.h`.
- Divisor de tensão TX0/D1 (5 V) → GPIO RX do Heltec (3.3 V) é obrigatório:
  3k3 / 1k8 típico.

**EEPROM "esquece" tudo a cada boot**
- Provavelmente o `EE_VERSION` foi bumpado e o firmware está resetando os
  defaults. Confirme em `config.h` que o valor não está sendo alterado entre
  builds.

## Estrutura do projeto

```
NANO_TX_v3/
├── platformio.ini           # 2 envs (old/new bootloader)
├── README.md                # este arquivo
└── src/
    ├── config.h             # pinos, constantes, endereços, magic EEPROM
    ├── crsf_tx.h            # cópia do encoder CRSF do v2 (header-only)
    ├── main.cpp             # setup + loop 50 Hz
    ├── storage.{h,cpp}      # EEPROM com magic+versão e commit diferido
    ├── inputs.{h,cpp}       # gimbals + switches + calib + AUX + PCF8574
    ├── trims.{h,cpp}        # debounce, edge, auto-repeat, offset
    ├── radio.{h,cpp}        # nRF24 (mesma config do v2)
    └── crsf.{h,cpp}         # frame CRSF dos 16 canais
```
