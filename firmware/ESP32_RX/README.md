# ESP32_RX — Flight Controller + RX + MAVLink

Firmware AIO (Flight Controller + RX nRF24L01 + telemetria MAVLink) para o
**OpenRC AeroLink**, com saída para MinimOSD-Extra e rádio SiK na mesma UART.

## O que faz

- **Recepção RC** via nRF24L01 (PA/LNA opcional).
- **AHRS** com MPU6050 (filtro complementar acel + giro).
- **PID** taxa-de-giro em roll/pitch (modo STAB).
- **Modo MANUAL** via switch SW1 do TX.
- **MAVLink v1** em UART2 alimentando:
  - **MinimOSD-Extra** (horizonte virtual, GPS, heading, battery).
  - **Rádio SiK** (telemetria full pra Mission Planner / QGC).
- **GPS** NMEA via TinyGPSPlus em UART1.
- **Barômetro** (placeholder — slot pronto pro BMP280 no I2C).

## Pinagem

| Função | GPIO | Notas |
|---|---|---|
| ESC PWM | 12 | |
| Servo L PWM | 13 | |
| Servo R PWM | 14 | |
| nRF24 CE | 4 | SPI VSPI |
| nRF24 CSN | 5 | |
| nRF24 SCK/MISO/MOSI | 18/19/23 | |
| MPU6050 / Baro SDA | 21 | I2C compartilhado |
| MPU6050 / Baro SCL | 22 | |
| GPS RX (Serial1) | 35 | input-only |
| GPS TX (Serial1) | 33 | reservado |
| MinimOSD TX (Serial2) | 17 | 57600 baud, MAVLink v1 |
| SiK RX (Serial2) | 16 | mesmo bus que MinimOSD via Y-split |

> O GPIO17 vai para **RX da OSD** e **RX do rádio SiK** simultaneamente (Y-split físico).
> O GPIO16 recebe o **TX do rádio SiK** via divisor de tensão 5V → 3.3V (resistores 10k/20k).

## Toolchain — PlatformIO

```bash
# 1. Instale PlatformIO Core (uma vez)
pip install platformio

# 2. Clone os headers do MAVLink em lib/mavlink/
git clone --recursive https://github.com/mavlink/c_library_v2 lib/mavlink

# 3. Compile + upload
pio run -t upload

# 4. Monitor serial
pio device monitor -b 115200
```

## Estrutura

```
ESP32_RX/
├── platformio.ini
├── src/
│   ├── main.cpp              # setup + criação das tasks
│   ├── config.h              # pinagem, baudrates, MAVLink IDs
│   ├── flight_data.h         # struct global + mutex
│   ├── mavlink_helper.h      # wrappers de empacotamento MAVLink
│   └── tasks/
│       ├── imu_task.*        # MPU6050 + AHRS
│       ├── radio_task.*      # nRF24L01 reader
│       ├── control_task.*    # PID + servos + failsafe
│       ├── gps_task.*        # TinyGPSPlus parser
│       ├── baro_task.*       # placeholder BMP280
│       ├── osd_task.*        # envia HEARTBEAT/ATTITUDE/GPS/VFR/SYS_STATUS/GPI
│       └── mavlink_rx_task.* # parser de comandos do GCS
└── lib/
    └── mavlink/              # c_library_v2 (clone manualmente)
```

## Stream MAVLink enviado

| Mensagem | Taxa | Conteúdo |
|---|---|---|
| `HEARTBEAT` | 1 Hz | tipo=FIXED_WING, modo, estado |
| `ATTITUDE` | 10 Hz | roll/pitch/yaw (rad) — alimenta horizonte |
| `GPS_RAW_INT` | 5 Hz | lat/lon/alt/sats |
| `VFR_HUD` | 5 Hz | heading, GS, throttle %, altitude |
| `SYS_STATUS` | 2 Hz | bateria (placeholder) |
| `GLOBAL_POSITION_INT` | 5 Hz | posição "fundida" |

> **Yaw** é integrado do giroscópio (MPU6050 não tem magnetômetro). O heading
> do compass na OSD vai derivar com o tempo — esse é o trade-off conhecido.

## Configurando o MinimOSD-Extra

1. Conecte a MinimOSD ao FTDI USB e abra o **MinimOSD-Extra Config Tool**.
2. Flashe o firmware mais recente (ramo `MAVLink`, não APM).
3. Em **Setup**, defina:
   - **MAVLink version**: `1.0`
   - **Baudrate**: `57600`
   - **Sysid**: `1` (mesmo que `MAV_SYSTEM_ID` em [config.h](src/config.h))
4. Em **Panel 1**, sugestão de layout:
   - Horizon (centro)
   - Roll/Pitch angle (canto superior)
   - GPS sats (canto superior esquerdo)
   - GS (canto inferior esquerdo)
   - Heading tape (topo)
   - Altitude (canto direito)
   - Battery voltage (canto inferior direito)
   - Flight mode (rodapé)
5. Salve no chip e desconecte o FTDI.

## Configurando rádio SiK

Use o **SiK Config Tool** ou comandos AT no terminal:
- Serial speed: `57` (57600 baud)
- Air speed: `64` (64 kbps)
- NetID: igual no rádio do GCS
- MAVLink framing: `1` (pacotes priorizados)

No GCS (Mission Planner / QGroundControl):
- Connect → COM do rádio @ 57600.
- Em ~10s deve aparecer `Connected to FC` e o ícone de avião.

## Debug

Em [config.h](src/config.h), troque:
```c
#define MAVLINK_DEBUG 1
```
Cada frame MAVLink enviado é impresso em hex no USB Serial (115200).
**Desligue em voo real** — overhead alto na UART de debug.

## Validação rápida

1. `pio run` → compila sem warnings.
2. Boot serial mostra `Tasks criadas. Sistema pronto.`
3. Conecte MinimOSD: horizonte aparece em ~5s e responde ao mover o ESP32.
4. Conecte SiK + GCS: heartbeat detectado, parâmetros listados (`KP_ROLL` etc).
5. Com GPS no céu aberto: sats > 4, lat/lon coerentes.

## Limitações conhecidas

- **Yaw deriva** sem magnetômetro (MPU6050 puro).
- **Altitude do barômetro = 0** até instalar BMP280 e descomentar bloco em
  [src/tasks/baro_task.cpp](src/tasks/baro_task.cpp).
- **PARAM_SET ecoa mas não aplica** — TODO ligar à `control_task`.
- **Bateria = 0** até instalar divisor de tensão e ler ADC.

## Histórico

O `.ino` antigo (`ESP32_RX.ino`) continua no diretório como referência —
PlatformIO compila apenas o conteúdo de `src/`, então não interfere.
