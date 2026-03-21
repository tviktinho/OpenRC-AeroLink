// FILE: MEGA.ino
// =============================================================================
// MEGA - Controlador Principal RC com Ponte H L298N + Servos
// Plataforma: Arduino Mega 2560
// Autor: OpenRC-AeroLink Project
// =============================================================================
// TESTE:
// 1. Conecte RX1 (pino 19) do MEGA <- TX0 (D1) do NANO_RX
// 2. Conecte GND comum entre MEGA e NANO_RX
// 3. Conecte L298N: IN1_A=8, IN2_A=9, IN1_B=10, IN2_B=11
// 4. Conecte servos em A0..A7 (até 8 servos)
// 5. Abra Serial Monitor a 115200 para ver debug
// 6. Mova os sticks e observe os valores CH1..CH7, switches e motores/servos
// 7. Se não receber frames, failsafe será ativado (motores param, servos neutro)
// =============================================================================

#include <Servo.h>

// =============================================================================
// CONFIGURAÇÃO DE PINOS - PONTE H L298N
// =============================================================================
#define IN1_A   8   // Motor A - Frente
#define IN2_A   9   // Motor A - Ré
#define IN1_B   10  // Motor B - Frente
#define IN2_B   11  // Motor B - Ré

// Opcional: Pinos ENA/ENB para PWM (se usar jumpers removidos no L298N)
// Descomente e configure se necessário
// #define USE_ENA_ENB
// #define ENA_PIN  6
// #define ENB_PIN  7

// =============================================================================
// CONFIGURAÇÃO DE PINOS - SERVOS
// =============================================================================
const uint8_t SERVO_PINS[] = {A0, A1, A2, A3, A4, A5, A6, A7};
#define NUM_SERVOS  8

// =============================================================================
// CONFIGURAÇÃO DO FRAME UART
// =============================================================================
#define FRAME_HEADER_1    0xAA
#define FRAME_HEADER_2    0x55
#define FRAME_PAYLOAD_LEN 11
#define FRAME_TOTAL_LEN   14  // 2 header + 11 payload + 1 checksum

// =============================================================================
// CONFIGURAÇÃO DE FAILSAFE
// =============================================================================
#define FAILSAFE_MS       150

// =============================================================================
// CONFIGURAÇÃO DE CONTROLE
// =============================================================================
// Direção dos eixos (1 = normal, -1 = invertido)
#define THROTTLE_DIR      1
#define STEERING_DIR      1

// Deadzone (0..127) - valores abaixo deste são considerados centro
#define DEADZONE          10

// Ganhos de mixagem tank drive
#define MIX_THROTTLE      1.0
#define MIX_STEERING      0.8

// =============================================================================
// MAPEAMENTO DE CANAIS
// =============================================================================
// p[0] = CH1 -> Servo 0 (A0)
// p[1] = CH2 -> Servo 1 (A1)
// p[2] = CH3 -> Throttle (motor)
// p[3] = CH4 -> Steering (motor mix)
// p[4] = CH5 -> Servo 2 (A2)
// p[5] = CH6 -> Servo 3 (A3)
// p[6] = CH7 -> Servo 4 (A4)
// Servos 5,6,7 (A5,A6,A7) ficam em neutro se não houver canais

#define CH_SERVO_0    0
#define CH_SERVO_1    1
#define CH_THROTTLE   2
#define CH_STEERING   3
#define CH_SERVO_2    4
#define CH_SERVO_3    5
#define CH_SERVO_4    6

// =============================================================================
// OBJETOS GLOBAIS
// =============================================================================
Servo servos[NUM_SERVOS];

// Buffer circular para recepção UART
#define RX_BUFFER_SIZE  64
uint8_t rxBuffer[RX_BUFFER_SIZE];
uint8_t rxHead = 0;
uint8_t rxTail = 0;

// Dados decodificados
uint8_t channels[7];    // p[0..6] = 0..255
uint8_t switches[4];    // s[0..3] = 0 ou 1

// Controle de tempo
unsigned long lastValidFrame = 0;
bool failsafeActive = true;

// =============================================================================
// FUNÇÕES AUXILIARES
// =============================================================================

// Converte byte (0..255) para microsegundos (1000..2000)
uint16_t byteToUs(uint8_t val) {
  return map(val, 0, 255, 1000, 2000);
}

// Converte byte (0..255) para valor com sinal (-127..+127)
int16_t byteToSigned(uint8_t val) {
  return (int16_t)val - 128;
}

// Aplica deadzone a um valor com sinal
int16_t applyDeadzone(int16_t val, int16_t dz) {
  if (abs(val) < dz) return 0;
  return val;
}

// Calcula checksum XOR
uint8_t calcChecksum(uint8_t* data, uint8_t len) {
  uint8_t chk = 0;
  for (uint8_t i = 0; i < len; i++) {
    chk ^= data[i];
  }
  return chk;
}

// Quantidade de bytes disponíveis no buffer circular
uint8_t rxAvailable() {
  return (RX_BUFFER_SIZE + rxHead - rxTail) % RX_BUFFER_SIZE;
}

// Lê um byte do buffer circular
uint8_t rxRead() {
  uint8_t val = rxBuffer[rxTail];
  rxTail = (rxTail + 1) % RX_BUFFER_SIZE;
  return val;
}

// Peek um byte do buffer circular sem remover
uint8_t rxPeek(uint8_t offset) {
  return rxBuffer[(rxTail + offset) % RX_BUFFER_SIZE];
}

// Descarta um byte do buffer
void rxDiscard() {
  rxTail = (rxTail + 1) % RX_BUFFER_SIZE;
}

// Lê bytes da Serial1 para o buffer circular
void rxFillBuffer() {
  while (Serial1.available() > 0) {
    uint8_t nextHead = (rxHead + 1) % RX_BUFFER_SIZE;
    if (nextHead != rxTail) {  // Buffer não cheio
      rxBuffer[rxHead] = Serial1.read();
      rxHead = nextHead;
    } else {
      break;  // Buffer cheio, para de ler
    }
  }
}

// Tenta parsear um frame válido do buffer
bool parseFrame() {
  // Precisa de pelo menos 14 bytes
  while (rxAvailable() >= FRAME_TOTAL_LEN) {
    // Procura header 0xAA 0x55
    if (rxPeek(0) == FRAME_HEADER_1 && rxPeek(1) == FRAME_HEADER_2) {
      // Header encontrado, extrai payload
      uint8_t payload[FRAME_PAYLOAD_LEN];
      
      // Pula header
      rxDiscard();
      rxDiscard();
      
      // Lê payload
      for (uint8_t i = 0; i < FRAME_PAYLOAD_LEN; i++) {
        payload[i] = rxRead();
      }
      
      // Lê checksum recebido
      uint8_t rxChecksum = rxRead();
      
      // Calcula checksum esperado
      uint8_t calcChk = calcChecksum(payload, FRAME_PAYLOAD_LEN);
      
      // Valida checksum
      if (rxChecksum == calcChk) {
        // Checksum OK! Extrai dados
        channels[0] = payload[0];
        channels[1] = payload[1];
        channels[2] = payload[2];
        channels[3] = payload[3];
        channels[4] = payload[4];
        channels[5] = payload[5];
        channels[6] = payload[6];
        switches[0] = payload[7];
        switches[1] = payload[8];
        switches[2] = payload[9];
        switches[3] = payload[10];
        
        return true;
      } else {
        // Checksum falhou - debug
        Serial.print(F("CHK FAIL: rx=0x"));
        Serial.print(rxChecksum, HEX);
        Serial.print(F(" calc=0x"));
        Serial.println(calcChk, HEX);
        // Continua procurando próximo header
      }
    } else {
      // Não é header, descarta byte e continua procurando
      rxDiscard();
    }
  }
  
  return false;
}

// Controla motor com ponte H L298N
void setMotor(uint8_t in1, uint8_t in2, int16_t speed) {
  // speed: -255 a +255
  speed = constrain(speed, -255, 255);
  
  if (speed > 0) {
    // Frente
    analogWrite(in1, speed);
    analogWrite(in2, 0);
  } else if (speed < 0) {
    // Ré
    analogWrite(in1, 0);
    analogWrite(in2, -speed);
  } else {
    // Parado
    analogWrite(in1, 0);
    analogWrite(in2, 0);
  }
}

// Aplica controle tank drive nos motores
void updateMotors() {
  // Converte canais para valores com sinal (-127..+127)
  int16_t throttle = byteToSigned(channels[CH_THROTTLE]);
  int16_t steering = byteToSigned(channels[CH_STEERING]);
  
  // Aplica direção
  throttle *= THROTTLE_DIR;
  steering *= STEERING_DIR;
  
  // Aplica deadzone
  throttle = applyDeadzone(throttle, DEADZONE);
  steering = applyDeadzone(steering, DEADZONE);
  
  // Tank mix: calcula velocidade de cada lado
  float fThrottle = throttle * MIX_THROTTLE;
  float fSteering = steering * MIX_STEERING;
  
  int16_t leftSpeed  = (int16_t)(fThrottle + fSteering);
  int16_t rightSpeed = (int16_t)(fThrottle - fSteering);
  
  // Escala para -255..+255
  leftSpeed  = constrain(leftSpeed * 2, -255, 255);
  rightSpeed = constrain(rightSpeed * 2, -255, 255);
  
  // Aplica nos motores
  setMotor(IN1_A, IN2_A, leftSpeed);
  setMotor(IN1_B, IN2_B, rightSpeed);
}

// Atualiza servos com os valores dos canais
void updateServos() {
  // Servo 0 <- CH1 (p[0])
  servos[0].writeMicroseconds(byteToUs(channels[CH_SERVO_0]));
  
  // Servo 1 <- CH2 (p[1])
  servos[1].writeMicroseconds(byteToUs(channels[CH_SERVO_1]));
  
  // Servo 2 <- CH5 (p[4])
  servos[2].writeMicroseconds(byteToUs(channels[CH_SERVO_2]));
  
  // Servo 3 <- CH6 (p[5])
  servos[3].writeMicroseconds(byteToUs(channels[CH_SERVO_3]));
  
  // Servo 4 <- CH7 (p[6])
  servos[4].writeMicroseconds(byteToUs(channels[CH_SERVO_4]));
  
  // Servos 5,6,7 ficam em neutro (não há canais suficientes)
  for (uint8_t i = 5; i < NUM_SERVOS; i++) {
    servos[i].writeMicroseconds(1500);
  }
}

// Aplica estado de failsafe
void applyFailsafe() {
  // Para motores
  setMotor(IN1_A, IN2_A, 0);
  setMotor(IN1_B, IN2_B, 0);
  
  // Servos em neutro
  for (uint8_t i = 0; i < NUM_SERVOS; i++) {
    servos[i].writeMicroseconds(1500);
  }
  
  // Zera canais
  for (uint8_t i = 0; i < 7; i++) {
    channels[i] = 128;
  }
  for (uint8_t i = 0; i < 4; i++) {
    switches[i] = 0;
  }
}

// Imprime debug dos valores recebidos
void printDebug() {
  static unsigned long lastPrint = 0;
  unsigned long now = millis();
  
  if (now - lastPrint >= 250) {  // 4 Hz
    lastPrint = now;
    
    Serial.print(F("CH:"));
    for (uint8_t i = 0; i < 7; i++) {
      Serial.print(channels[i]);
      Serial.print(F(","));
    }
    Serial.print(F(" SW:"));
    for (uint8_t i = 0; i < 4; i++) {
      Serial.print(switches[i]);
      Serial.print(F(","));
    }
    
    // Mostra valores de motor calculados
    int16_t throttle = byteToSigned(channels[CH_THROTTLE]) * THROTTLE_DIR;
    int16_t steering = byteToSigned(channels[CH_STEERING]) * STEERING_DIR;
    throttle = applyDeadzone(throttle, DEADZONE);
    steering = applyDeadzone(steering, DEADZONE);
    
    Serial.print(F(" T:"));
    Serial.print(throttle);
    Serial.print(F(" S:"));
    Serial.print(steering);
    
    if (failsafeActive) {
      Serial.print(F(" [FAILSAFE]"));
    }
    
    Serial.println();
  }
}

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  // Inicializa Serial USB para debug
  Serial.begin(115200);
  Serial.println(F("=== MEGA Controlador RC ==="));
  
  // IMPORTANTE: Pino 53 como OUTPUT para SPI master (se usar nRF no futuro)
  pinMode(53, OUTPUT);
  
  // Inicializa Serial1 para receber frames do NANO_RX
  Serial1.begin(115200);
  Serial.println(F("Serial1 iniciada (RX1 pino 19)"));
  
  // Configura pinos da ponte H
  pinMode(IN1_A, OUTPUT);
  pinMode(IN2_A, OUTPUT);
  pinMode(IN1_B, OUTPUT);
  pinMode(IN2_B, OUTPUT);
  
  // Inicializa motores parados
  setMotor(IN1_A, IN2_A, 0);
  setMotor(IN1_B, IN2_B, 0);
  Serial.println(F("Ponte H L298N configurada"));
  
  #ifdef USE_ENA_ENB
  pinMode(ENA_PIN, OUTPUT);
  pinMode(ENB_PIN, OUTPUT);
  analogWrite(ENA_PIN, 255);
  analogWrite(ENB_PIN, 255);
  Serial.println(F("ENA/ENB configurados"));
  #endif
  
  // Inicializa servos
  for (uint8_t i = 0; i < NUM_SERVOS; i++) {
    servos[i].attach(SERVO_PINS[i]);
    servos[i].writeMicroseconds(1500);  // Neutro
  }
  Serial.print(F("Servos inicializados: "));
  Serial.println(NUM_SERVOS);
  
  // Inicializa canais com valores neutros
  for (uint8_t i = 0; i < 7; i++) {
    channels[i] = 128;
  }
  for (uint8_t i = 0; i < 4; i++) {
    switches[i] = 0;
  }
  
  lastValidFrame = millis();
  
  Serial.println(F("=== MEGA Pronto ==="));
  Serial.print(F("Failsafe timeout: "));
  Serial.print(FAILSAFE_MS);
  Serial.println(F(" ms"));
}

// =============================================================================
// LOOP PRINCIPAL
// =============================================================================
void loop() {
  unsigned long currentTime = millis();
  
  // Preenche buffer com dados da Serial1
  rxFillBuffer();
  
  // Tenta parsear um frame
  if (parseFrame()) {
    lastValidFrame = currentTime;
    failsafeActive = false;
    
    // Atualiza saídas
    updateMotors();
    updateServos();
  }
  
  // Verifica failsafe
  if (currentTime - lastValidFrame > FAILSAFE_MS) {
    if (!failsafeActive) {
      failsafeActive = true;
      Serial.println(F("!!! FAILSAFE ATIVO !!!"));
      applyFailsafe();
    }
  }
  
  // Debug
  printDebug();
}
