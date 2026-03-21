# ESP32 Flight Controller — OpenRC AeroLink

Firmware de **controladora de voo** para ESP32, projetado para **asa-volante (flying wing)** com elevons.  
Recebe comandos do transmissor **NANO_TX** via rádio nRF24L01 e controla servos/ESC com mixagem elevon completa.  
Inclui telemetria **MAVLink v1** e AHRS via MPU6050.

---

## ✨ Funcionalidades

| Recurso | Descrição |
|---------|-----------|
| 🎛️ **Elevon Mixing** | Combina pitch + roll em L/R com ganhos, expo e diferencial |
| 📡 **nRF24L01** | Recepção de 8 canais analógicos + 2 switches do NANO_TX |
| 🧭 **AHRS** | Filtro complementar (gyro + acelerômetro) a 50 Hz via MPU6050 |
| 📊 **MAVLink v1** | Heartbeat, Attitude e RC Channels via UART (compatível QGroundControl) |
| 💾 **NVS** | Trims de RC e servo salvos na flash (persistem após reset) |
| 🛡️ **Failsafe** | Motor corta e servos vão para neutro após 200ms sem sinal |
| ⚙️ **Parâmetros** | Trims e deadzone ajustáveis via MAVLink PARAM_SET |

---

## 🔧 Hardware Necessário

| Componente | Modelo | Qtd |
|-----------|--------|-----|
| Microcontrolador | **ESP32 DevKit v1** (ou compatível) | 1 |
| IMU | **MPU6050** (GY-521) | 1 |
| Rádio | **nRF24L01** (PA/LNA recomendado) | 1 |
| ESC | Qualquer ESC brushless com entrada PWM | 1 |
| Servos | 2× micro servo (9g recomendado) | 2 |
| Regulador | BEC 5V (para servos) | 1 |

> [!TIP]
> Use o módulo nRF24L01 **PA/LNA** (com antena externa) para maior alcance.  
> Adicione um **capacitor de 10µF** entre VCC e GND do módulo nRF24 para estabilidade.

---

## 🔌 Diagrama de Conexões

### ESP32 ↔ nRF24L01

```
┌─────────────────────────────────────────┐
│              ESP32 DevKit               │
├─────────────────────────────────────────┤
│  nRF24L01          │  Pino ESP32        │
│  ─────────         │  ──────────        │
│  VCC               │  3.3V              │
│  GND               │  GND               │
│  CE                │  GPIO 4            │
│  CSN               │  GPIO 5            │
│  MOSI              │  GPIO 23 (VSPI)    │
│  MISO              │  GPIO 19 (VSPI)    │
│  SCK               │  GPIO 18 (VSPI)    │
│  IRQ               │  (não conectado)   │
└─────────────────────────────────────────┘
```

> [!WARNING]
> O nRF24L01 opera a **3.3V**. Não alimente com 5V, isso danificará o módulo.

### ESP32 ↔ MPU6050

```
┌─────────────────────────────────────────┐
│  MPU6050 (GY-521)  │  Pino ESP32        │
│  ─────────         │  ──────────        │
│  VCC               │  3.3V              │
│  GND               │  GND               │
│  SDA               │  GPIO 21           │
│  SCL               │  GPIO 22           │
└─────────────────────────────────────────┘
```

### ESP32 ↔ Servos / ESC

```
┌─────────────────────────────────────────┐
│  Atuador           │  Pino ESP32        │
│  ─────────         │  ──────────        │
│  ESC (sinal)       │  GPIO 12           │
│  Servo L (sinal)   │  GPIO 13           │
│  Servo R (sinal)   │  GPIO 14           │
│                    │                    │
│  ⚠️ GND dos servos/ESC = GND do ESP32  │
│  ⚠️ VCC dos servos = BEC 5V (não USB!) │
└─────────────────────────────────────────┘
```

### ESP32 ↔ UART MAVLink (Raspberry Pi / GCS)

```
┌─────────────────────────────────────────┐
│  Dispositivo       │  Pino ESP32        │
│  ─────────         │  ──────────        │
│  RPi TX / GCS TX   │  GPIO 26 (RX2)     │
│  RPi RX / GCS RX   │  GPIO 27 (TX2)     │
│  GND               │  GND               │
└─────────────────────────────────────────┘
```

### Resumo Visual Completo

```
                    ┌──────────────┐
                    │   NANO_TX    │
                    │ (Transmissor)│
                    └──────┬───────┘
                           │ nRF24L01 (2.4 GHz)
                           ▼
  ┌──────────────────────────────────────────────┐
  │                   ESP32                       │
  │                                               │
  │  ┌─────────┐  ┌─────────┐  ┌──────────────┐ │
  │  │ nRF24L01│  │ MPU6050 │  │   Serial2    │ │
  │  │ (GPIO   │  │ (I2C)   │  │ (MAVLink)    │ │
  │  │  4,5)   │  │ (21,22) │  │ (26,27)      │ │
  │  └────┬────┘  └────┬────┘  └──────┬───────┘ │
  │       │            │              │          │
  │  ┌────┴────────────┴──────────────┘          │
  │  │         LOOP PRINCIPAL                    │
  │  │  Radio → Mixing → Servos/ESC             │
  │  │  MPU   → AHRS → MAVLink Attitude         │
  │  └──────────┬────────────┬──────────┘        │
  │             │            │                   │
  │        GPIO 12      GPIO 13,14               │
  └─────────┬──────────────┬─────────────────────┘
            │              │
       ┌────┴────┐    ┌────┴────┐
       │   ESC   │    │ Servos  │
       │ (Motor) │    │  L / R  │
       └─────────┘    └─────────┘
```

---

## 📡 Protocolo de Rádio (nRF24L01)

### Configuração

| Parâmetro | Valor |
|-----------|-------|
| Endereço | `"00001"` |
| Canal | 76 |
| Data Rate | 250 Kbps |
| PA Level | LOW |
| AutoAck | Desabilitado |

### Estrutura do Pacote

```c
struct Packet {
  uint8_t p[8];   // 8 canais analógicos (0..255)
  uint8_t s1;     // Switch 1
  uint8_t s2;     // Switch 2
};  // Total: 10 bytes
```

### Mapeamento de Canais

| Índice | Potenciômetro TX | Função no FC |
|--------|------------------|-------------|
| `p[0]` | A0 | **Throttle** (0→min, 255→max) |
| `p[1]` | A1 | (livre) |
| `p[2]` | A2 | **Aileron / Roll** (128=centro) |
| `p[3]` | A3 | **Elevator / Pitch** (128=centro) |
| `p[4]` | A4 | (livre) |
| `p[5]` | A5 | (livre) |
| `p[6]` | A6 | (livre) |
| `p[7]` | A7 | (livre) |
| `s1` | Switch 1 | (reservado — estabilização futura) |
| `s2` | Switch 2 | (reservado) |

---

## 🎛️ Pipeline de Controle (Elevon Mixing)

O firmware processa os sticks de aileron e elevator e combina em sinais para os dois servos elevon:

```
Stick (byte 0-255)
     │
     ▼
mapByteToNorm()  →  -1.0 ... +1.0  (centrado em 128)
     │
     ▼
applyExpo()      →  suaviza o centro, mantém throws
     │
     ▼
× Ganho (G_E, G_A)  →  amplifica a resposta (2.55×)
     │
     ▼
Elevon Mixing:   L = E - A
                 R = E + A
     │
     ▼
Diferencial      →  30% menos deflexão para baixo (DOWN)
     │
     ▼
normToUs()       →  1100 ... 1900 µs  (±400µs do centro)
     │
     ▼
+ Reflex (+30µs) →  trim para cima (auto-sustentação)
+ Servo Trim     →  ajuste fino por canal
     │
     ▼
constrain(1000, 2000)  →  saturação de segurança
     │
     ▼
writeMicroseconds()    →  sinal PWM para o servo
```

### Parâmetros de Mixing

| Parâmetro | Valor | Descrição |
|-----------|-------|-----------|
| `G_E` | 2.55 | Ganho de pitch (throw) |
| `G_A` | 2.55 | Ganho de roll (throw) |
| `EXPO_E` | 0.25 | Expo de pitch (suaviza centro) |
| `EXPO_A` | 0.25 | Expo de roll (suaviza centro) |
| `DIFF` | -0.30 | Diferencial (30% menos para baixo) |
| `REFLEX_US` | +30 | Reflex em µs (trim permanente para cima) |

> [!NOTE]
> Esses valores são idênticos ao firmware **14-BRIZA** que funciona comprovadamente em voo.  
> Ajuste `G_E` e `G_A` se precisar de mais ou menos throw.  
> O expo de 0.25 deixa o centro suave sem perder resolução nos extremos.

---

## 🧭 AHRS (Attitude and Heading Reference System)

O firmware calcula atitude (roll/pitch) usando um **filtro complementar** a 50 Hz:

```
roll  = 0.98 × (roll  + gyro_x × dt) + 0.02 × accel_roll
pitch = 0.98 × (pitch + gyro_y × dt) + 0.02 × accel_pitch
```

- **Alpha = 0.98** → confia mais no giroscópio (rápido), suavizado pelo acelerômetro (estável)
- Yaw não é calculado (sem magnetômetro)
- Os dados são enviados via MAVLink como `ATTITUDE` a 20 Hz

---

## 📊 Telemetria MAVLink

O firmware envia 3 tipos de mensagens pela **Serial2** (UART):

| Mensagem | Frequência | Conteúdo |
|----------|-----------|----------|
| `HEARTBEAT` | 1 Hz | Status do sistema (type=GENERIC, state=ACTIVE) |
| `ATTITUDE` | 20 Hz | Roll, Pitch em radianos |
| `RC_CHANNELS_RAW` | 20 Hz | 8 canais RC em µs (1000–2000) |

### Recepção de Comandos

O firmware aceita `PARAM_SET` para ajustar parâmetros remotamente:

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `TRIM1`…`TRIM8` | int | Trim de RC por canal (em µs) |
| `SRV_L` | int | Trim do servo esquerdo |
| `SRV_R` | int | Trim do servo direito |
| `DEADZ` | float | Deadzone (0.0 – 0.45) |

> Os parâmetros são salvos automaticamente na **NVS** (flash) e persistem após reset.

---

## 🛡️ Failsafe

| Condição | Timeout | Ação |
|----------|---------|------|
| Sem pacote do rádio | 200 ms | Motor → IDLE (1000µs), Servos → neutro + reflex |
| | | MAVLink/AHRS continuam funcionando |

---

## 🚀 Compilação e Upload

### Requisitos

1. **Arduino IDE** 2.x (ou 1.8.x)
2. **Board Manager** → ESP32 by Espressif Systems
3. **Bibliotecas** (instalar via Library Manager):

| Biblioteca | Versão mínima |
|-----------|--------------|
| `RF24` (by TMRh20) | 1.4.x |
| `Adafruit MPU6050` | 2.2.x |
| `Adafruit Unified Sensor` | 1.1.x |
| `ESP32Servo` | 1.1.x |

4. **MAVLink** — já incluída na pasta `mavlink/` do projeto (não precisa instalar)

### Configuração do Arduino IDE

```
Placa:          ESP32 Dev Module
Upload Speed:   921600
CPU Frequency:  240 MHz (WiFi/BT)
Flash Frequency: 80 MHz
Flash Mode:     QIO
Flash Size:     4MB (32Mb)
Partition:      Default 4MB with spiffs
PSRAM:          Disabled
Port:           (a COM do seu ESP32)
```

### Passos

1. Abra `firmware/ESP32/ESP32.ino` no Arduino IDE
2. Selecione a placa **ESP32 Dev Module**
3. Selecione a porta COM correta
4. Clique em **Upload** (→)
5. Abra o **Serial Monitor** a 115200 baud para debug

---

## 📁 Estrutura de Arquivos

```
firmware/ESP32/
├── ESP32.ino              # Firmware principal
├── firmware_0x00.bin      # Binário pré-compilado (versão anterior)
└── mavlink/               # Biblioteca MAVLink v1 (local)
    └── common/
        ├── mavlink.h
        ├── mavlink_msg_heartbeat.h
        ├── mavlink_msg_attitude.h
        ├── mavlink_msg_rc_channels_raw.h
        ├── mavlink_msg_param_set.h
        ├── mavlink_msg_param_value.h
        └── ...
```

---

## 🧪 Teste no Solo (Bench Test)

### Checklist antes de voar

- [ ] Alimentar ESP32 via USB e verificar `Serial Monitor` → "ESP32 MAVLink Autopilot - Inicializando"
- [ ] Verificar que **MPU6050** e **NRF24** são detectados (sem mensagens de erro)
- [ ] Ligar o transmissor (NANO_TX) e verificar que servos respondem
- [ ] Mover stick de **aileron** → ambos servos movem em direções opostas (elevon)
- [ ] Mover stick de **elevator** → ambos servos movem na mesma direção
- [ ] Stick no centro → servos ficam em ~1530µs (1500 + 30µs reflex)
- [ ] Throttle no mínimo → ESC em 1000µs (motor cortado)
- [ ] Desligar transmissor → após 200ms, motor corta e servos vão para neutro (**failsafe**)
- [ ] Verificar MAVLink conectando QGroundControl via UART (115200 baud)

### Verificação de sentido dos servos

Se um servo estiver invertido, troque os sinais `srvL` / `srvR` nos pinos ou inverta o sinal na mistura:
```c
// Inverter L: trocar de (E - A) para (A - E)
float L = -(E - A);  // inverte servo esquerdo
```

---

## ⚙️ Ajustes e Calibração

### Ajustar Throws (Ganho)

```c
float G_E = 2.55f;   // ↑ mais deflexão em pitch
float G_A = 2.55f;   // ↑ mais deflexão em roll
```
- Valores maiores = mais deflexão, avião mais ágil
- Valores menores = menos deflexão, avião mais estável
- Recomendado: 1.5 – 3.0

### Ajustar Expo

```c
float EXPO_E = 0.25f;  // 0 = linear, 0.5 = muito expo
float EXPO_A = 0.25f;
```
- Expo suaviza o centro sem perder resolução nos extremos
- Valores baixos (0.1) = quase linear
- Valores altos (0.5) = centro muito "macio"

### Ajustar Diferencial

```c
float DIFF = -0.30f;  // negativo = menos DOWN
```
- `-0.30` = 30% menos deflexão para baixo que para cima
- `0` = sem diferencial (simétrico)

### Ajustar Reflex

```c
int REFLEX_US = +30;  // positivo = trim para cima
```
- Compensa a tendência de nose-down em asa-voadora
- Ajuste conforme o CG do avião

---

## 📌 Compatibilidade

| Transmissor | Compatível | Notas |
|-------------|-----------|-------|
| NANO_TX (OpenRC AeroLink) | ✅ | Protocolo nativo, mesma struct |
| 14-BRIZA | ✅ | Mesmo mapeamento de canais |
| Outros TX nRF24L01 | ⚠️ | Precisa usar mesma struct Packet e endereço |

---

## 📄 Licença

Distribuído sob licença **MIT**. Consulte o arquivo [LICENSE](../../LICENSE).
