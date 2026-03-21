// FILE: NANO_RX.ino
// =============================================================================
// NANO_RX - Receptor RC com nRF24L01 + Servos Locais + UART para MEGA
// Plataforma: Arduino Nano (ATmega328P)
// Autor: OpenRC-AeroLink Project
// =============================================================================
// TESTE:
// 1. Conecte nRF24L01 aos pinos CE=8, CSN=7, MOSI=11, MISO=12, SCK=13
// 2. Conecte TX0 (D1) do Nano -> RX1 (pino 19) do MEGA
// 3. Conecte GND comum entre Nano e MEGA
// 4. Para debug, conecte um adaptador FTDI no pino DEBUG_TX_PIN (D4)
// 5. LED_BUILTIN pisca a cada pacote RF recebido
// 6. Serial (TX0) envia APENAS dados binários para o MEGA
// =============================================================================

#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <Servo.h>
#include <SoftwareSerial.h>

// =============================================================================
// CONFIGURAÇÃO DE PINOS
// =============================================================================
#define PIN_NRF_CE    8
#define PIN_NRF_CSN   7

// Pino para debug via SoftwareSerial (TX only)
#define DEBUG_TX_PIN  4
#define DEBUG_RX_PIN  2  // Não usado, mas necessário para SoftwareSerial

// LED para indicar recepção
#define LED_PIN       LED_BUILTIN

// Pinos dos servos locais (até 8 servos)
const uint8_t SERVO_PINS[] = {3, 5, 6, 9, 10, 14, 15, 16};
#define NUM_SERVOS    8

// =============================================================================
// CONFIGURAÇÃO RF
// =============================================================================
#define RF_CHANNEL    76
const byte RF_ADDRESS[6] = "01010";

// =============================================================================
// CONFIGURAÇÃO DE TEMPO
// =============================================================================
#define FAILSAFE_TIMEOUT_MS  500
#define LED_BLINK_MS         50

// =============================================================================
// CONFIGURAÇÃO DO FRAME UART
// =============================================================================
#define FRAME_HEADER_1    0xAA
#define FRAME_HEADER_2    0x55
#define FRAME_PAYLOAD_LEN 11
#define FRAME_TOTAL_LEN   14  // 2 header + 11 payload + 1 checksum

// =============================================================================
// ESTRUTURA DO PACOTE RF
// =============================================================================
struct Packet {
  uint8_t p[7];   // 7 canais analógicos (0..255)
  uint8_t s[4];   // 4 switches (0 ou 1)
};

// =============================================================================
// OBJETOS GLOBAIS
// =============================================================================
RF24 radio(PIN_NRF_CE, PIN_NRF_CSN);
Servo servos[NUM_SERVOS];
SoftwareSerial debugSerial(DEBUG_RX_PIN, DEBUG_TX_PIN);  // RX, TX

Packet pkt;
unsigned long lastPacketTime = 0;
unsigned long ledOffTime = 0;
bool failsafeActive = true;

// Buffer do frame UART
uint8_t uartFrame[FRAME_TOTAL_LEN];

// =============================================================================
// FUNÇÕES AUXILIARES
// =============================================================================

// Converte byte (0..255) para microsegundos (1000..2000)
uint16_t byteToUs(uint8_t val) {
  return map(val, 0, 255, 1000, 2000);
}

// Calcula checksum XOR do payload
uint8_t calcChecksum(uint8_t* payload, uint8_t len) {
  uint8_t chk = 0;
  for (uint8_t i = 0; i < len; i++) {
    chk ^= payload[i];
  }
  return chk;
}

// Monta e envia o frame UART para o MEGA
void sendUartFrame() {
  // Header
  uartFrame[0] = FRAME_HEADER_1;
  uartFrame[1] = FRAME_HEADER_2;
  
  // Payload: p0..p6, s0..s3
  uartFrame[2] = pkt.p[0];
  uartFrame[3] = pkt.p[1];
  uartFrame[4] = pkt.p[2];
  uartFrame[5] = pkt.p[3];
  uartFrame[6] = pkt.p[4];
  uartFrame[7] = pkt.p[5];
  uartFrame[8] = pkt.p[6];
  uartFrame[9] = pkt.s[0];
  uartFrame[10] = pkt.s[1];
  uartFrame[11] = pkt.s[2];
  uartFrame[12] = pkt.s[3];
  
  // Checksum (XOR dos 11 bytes de payload)
  uartFrame[13] = calcChecksum(&uartFrame[2], FRAME_PAYLOAD_LEN);
  
  // Envia frame binário via Serial hardware (TX0/D1)
  Serial.write(uartFrame, FRAME_TOTAL_LEN);
}

// Aplica valores nos servos locais
void updateServos() {
  // Mapeia canais p[0..6] para servos 0..6
  // Servo 7 fica em neutro se não houver canal correspondente
  for (uint8_t i = 0; i < NUM_SERVOS; i++) {
    if (i < 7) {
      servos[i].writeMicroseconds(byteToUs(pkt.p[i]));
    } else {
      servos[i].writeMicroseconds(1500);  // Neutro para servos extras
    }
  }
}

// Aplica estado de failsafe
void applyFailsafe() {
  // Zera todos os valores do pacote
  for (uint8_t i = 0; i < 7; i++) {
    pkt.p[i] = 128;  // Centro (neutro)
  }
  for (uint8_t i = 0; i < 4; i++) {
    pkt.s[i] = 0;    // Switches desligados
  }
  
  // Atualiza servos para posição neutra
  for (uint8_t i = 0; i < NUM_SERVOS; i++) {
    servos[i].writeMicroseconds(1500);
  }
}

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  // Inicializa Serial hardware para comunicação binária com MEGA
  Serial.begin(115200);
  
  // Inicializa SoftwareSerial para debug
  debugSerial.begin(115200);
  debugSerial.println(F("=== NANO_RX Inicializando ==="));
  
  // Configura LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // Inicializa servos
  for (uint8_t i = 0; i < NUM_SERVOS; i++) {
    servos[i].attach(SERVO_PINS[i]);
    servos[i].writeMicroseconds(1500);  // Posição neutra inicial
  }
  debugSerial.print(F("Servos inicializados: "));
  debugSerial.println(NUM_SERVOS);
  
  // Inicializa pacote com valores neutros
  for (uint8_t i = 0; i < 7; i++) pkt.p[i] = 128;
  for (uint8_t i = 0; i < 4; i++) pkt.s[i] = 0;
  
  // Inicializa rádio nRF24L01
  if (!radio.begin()) {
    debugSerial.println(F("ERRO: nRF24L01 nao encontrado!"));
    while (1) {
      digitalWrite(LED_PIN, HIGH);
      delay(100);
      digitalWrite(LED_PIN, LOW);
      delay(100);
    }
  }
  
  // Configura parâmetros do rádio
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_250KBPS);
  radio.setChannel(RF_CHANNEL);
  radio.setAutoAck(false);
  radio.openReadingPipe(0, RF_ADDRESS);
  radio.startListening();
  
  debugSerial.println(F("nRF24L01 configurado como RX"));
  debugSerial.print(F("Canal: "));
  debugSerial.println(RF_CHANNEL);
  debugSerial.print(F("Failsafe timeout: "));
  debugSerial.print(FAILSAFE_TIMEOUT_MS);
  debugSerial.println(F(" ms"));
  debugSerial.println(F("=== RX Pronto ==="));
  
  lastPacketTime = millis();
}

// =============================================================================
// LOOP PRINCIPAL
// =============================================================================
void loop() {
  unsigned long currentTime = millis();
  
  // Verifica se há pacote RF disponível
  if (radio.available()) {
    // Lê pacote
    radio.read(&pkt, sizeof(pkt));
    lastPacketTime = currentTime;
    failsafeActive = false;
    
    // Liga LED
    digitalWrite(LED_PIN, HIGH);
    ledOffTime = currentTime + LED_BLINK_MS;
    
    // Atualiza servos locais
    updateServos();
    
    // Envia frame UART para o MEGA
    sendUartFrame();
    
    // Debug (a cada 500ms para não sobrecarregar)
    static unsigned long lastDebugTime = 0;
    if (currentTime - lastDebugTime >= 500) {
      lastDebugTime = currentTime;
      
      debugSerial.print(F("RX P["));
      for (uint8_t i = 0; i < 7; i++) {
        debugSerial.print(pkt.p[i]);
        if (i < 6) debugSerial.print(F(","));
      }
      debugSerial.print(F("] S["));
      for (uint8_t i = 0; i < 4; i++) {
        debugSerial.print(pkt.s[i]);
        if (i < 3) debugSerial.print(F(","));
      }
      debugSerial.println(F("]"));
    }
  }
  
  // Desliga LED após tempo de blink
  if (ledOffTime > 0 && currentTime >= ledOffTime) {
    digitalWrite(LED_PIN, LOW);
    ledOffTime = 0;
  }
  
  // Verifica failsafe
  if (currentTime - lastPacketTime > FAILSAFE_TIMEOUT_MS) {
    if (!failsafeActive) {
      failsafeActive = true;
      debugSerial.println(F("!!! FAILSAFE ATIVO !!!"));
    }
    
    // Aplica failsafe
    applyFailsafe();
    
    // Continua enviando frames neutros para o MEGA
    static unsigned long lastFailsafeFrame = 0;
    if (currentTime - lastFailsafeFrame >= 50) {  // 20 Hz durante failsafe
      lastFailsafeFrame = currentTime;
      sendUartFrame();
    }
    
    // Pisca LED lentamente durante failsafe
    static unsigned long lastFailsafeBlink = 0;
    if (currentTime - lastFailsafeBlink >= 500) {
      lastFailsafeBlink = currentTime;
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }
  }
}
