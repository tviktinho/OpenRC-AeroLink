// FILE: NANO_TX.ino
// =============================================================================
// NANO_TX - Transmissor RC com 7 Potenciômetros + 4 Switches
// Plataforma: Arduino Nano (ATmega328P)
// Autor: OpenRC-AeroLink Project
// =============================================================================
// PROBLEMA COMUM: Shield apaga durante transmissão
// CAUSA: nRF24L01 consome picos de ~15mA em TX, regulador 3.3V do Nano é fraco
// SOLUÇÕES IMPLEMENTADAS:
//   1. RF24_PA_MIN - potência mínima (menos consumo)
//   2. Delay após radio.write() para estabilizar tensão
//   3. Capacitor de 10-100uF recomendado entre VCC e GND do nRF24L01
//   4. Taxa de TX reduzida para 50Hz (20ms)
//   5. PowerDown entre transmissões
// =============================================================================
// TESTE:
// 1. Conecte o nRF24L01 aos pinos CE=8, CSN=7, MOSI=11, MISO=12, SCK=13
// 2. IMPORTANTE: Adicione capacitor 10-100uF entre VCC e GND do nRF24L01
// 3. Conecte potenciômetros em A0..A6
// 4. Conecte switches em D2,D3,D4,D5 (INPUT_PULLUP, ativo LOW)
// 5. Abra Serial Monitor a 115200 para ver valores de debug
// =============================================================================

#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

// =============================================================================
// CONFIGURAÇÃO DE PINOS
// =============================================================================
#define PIN_NRF_CE    8
#define PIN_NRF_CSN   7

// Potenciômetros (A0..A6)
#define PIN_POT_0     A0
#define PIN_POT_1     A1
#define PIN_POT_2     A2
#define PIN_POT_3     A3
#define PIN_POT_4     A4
#define PIN_POT_5     A5
#define PIN_POT_6     A6

// Switches (INPUT_PULLUP, ativo LOW = pressionado)
#define SW1_PIN       2
#define SW2_PIN       3
#define SW3_PIN       4
#define SW4_PIN       5

// LED para indicar status
#define LED_PIN       LED_BUILTIN

// =============================================================================
// CONFIGURAÇÃO RF - OTIMIZADA PARA BAIXO CONSUMO
// =============================================================================
#define RF_CHANNEL    76
const byte RF_ADDRESS[6] = "0001";

// =============================================================================
// CONFIGURAÇÃO DE TEMPO
// =============================================================================
#define TX_INTERVAL_MS  20  // 50 Hz (reduzido de 100Hz para economizar energia)
#define RADIO_SETTLE_MS 2   // Tempo para tensão estabilizar após TX

// =============================================================================
// ESTRUTURA DO PACOTE
// =============================================================================
struct Packet {
  uint8_t p[7];   // 7 canais analógicos (A0..A6) mapeados para 0..255
  uint8_t s[4];   // 4 switches (cada 0 ou 1)
};

// =============================================================================
// OBJETOS GLOBAIS
// =============================================================================
RF24 radio(PIN_NRF_CE, PIN_NRF_CSN);
Packet pkt;
unsigned long lastTxTime = 0;
uint32_t txCount = 0;
uint32_t txFailCount = 0;
bool radioOk = false;

// =============================================================================
// INICIALIZAÇÃO DO RÁDIO COM RETRY
// =============================================================================
bool initRadio() {
  // Aguarda estabilização da alimentação
  delay(100);
  
  // Tenta inicializar o rádio até 3 vezes
  for (uint8_t attempt = 0; attempt < 3; attempt++) {
    Serial.print(F("Tentativa ")); Serial.print(attempt + 1); Serial.println(F("/3..."));
    
    if (radio.begin()) {
      // Configurações otimizadas para BAIXO CONSUMO
      radio.setPALevel(RF24_PA_MIN);      // MÍNIMA potência = MÍNIMO consumo
      radio.setDataRate(RF24_250KBPS);    // Menor taxa = maior alcance, menor consumo
      radio.setChannel(RF_CHANNEL);
      radio.setAutoAck(false);            // Sem ACK = menos transmissões
      radio.setRetries(0, 0);             // Sem retries automáticos
      radio.setPayloadSize(sizeof(Packet)); // Payload fixo
      radio.openWritingPipe(RF_ADDRESS);
      radio.stopListening();
      
      // Coloca em PowerDown para economizar até próxima TX
      radio.powerDown();
      delay(5);
      
      Serial.println(F("Radio OK!"));
      return true;
    }
    
    delay(100);
  }
  
  Serial.println(F("FALHA: Radio nao iniciou!"));
  return false;
}

// =============================================================================
// TRANSMISSÃO COM PROTEÇÃO DE ENERGIA
// =============================================================================
bool safeTx(void* data, uint8_t len) {
  // Acorda o rádio
  radio.powerUp();
  delayMicroseconds(1500);  // Tempo de wake-up do nRF24L01 (1.5ms)
  
  // Transmite
  bool ok = radio.write(data, len);
  
  // Volta para PowerDown imediatamente (economiza ~12mA)
  radio.powerDown();
  
  // Pequeno delay para tensão estabilizar
  delay(RADIO_SETTLE_MS);
  
  return ok;
}

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  // Configura LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);  // LED ligado durante init
  
  // Inicializa Serial para debug
  Serial.begin(115200);
  delay(100);  // Aguarda Serial estabilizar
  
  Serial.println();
  Serial.println(F("==========================================="));
  Serial.println(F("  NANO_TX - Transmissor RC (Low Power)"));
  Serial.println(F("==========================================="));
  Serial.println(F("DICA: Coloque capacitor 10-100uF no nRF24!"));
  Serial.println();

  // Configura pinos dos switches como INPUT_PULLUP
  pinMode(SW1_PIN, INPUT_PULLUP);
  pinMode(SW2_PIN, INPUT_PULLUP);
  pinMode(SW3_PIN, INPUT_PULLUP);
  pinMode(SW4_PIN, INPUT_PULLUP);

  // Inicializa rádio com retry
  radioOk = initRadio();
  
  if (!radioOk) {
    Serial.println(F("!!! MODO OFFLINE - Sem radio !!!"));
    // Pisca LED rápido para indicar erro
    while (1) {
      digitalWrite(LED_PIN, HIGH); delay(100);
      digitalWrite(LED_PIN, LOW);  delay(100);
    }
  }

  Serial.println(F("-------------------------------------------"));
  Serial.print(F("Canal RF: ")); Serial.println(RF_CHANNEL);
  Serial.print(F("Potencia: ")); Serial.println(F("MIN (economia)"));
  Serial.print(F("Taxa TX: ")); Serial.print(1000/TX_INTERVAL_MS); Serial.println(F(" Hz"));
  Serial.print(F("Payload: ")); Serial.print(sizeof(Packet)); Serial.println(F(" bytes"));
  Serial.println(F("-------------------------------------------"));
  Serial.println(F("=== TX Pronto ==="));
  Serial.println();
  
  digitalWrite(LED_PIN, LOW);  // LED desliga após init OK
}

// =============================================================================
// LOOP PRINCIPAL
// =============================================================================
void loop() {
  unsigned long currentTime = millis();

  // Controle de taxa de transmissão (50 Hz)
  if (currentTime - lastTxTime >= TX_INTERVAL_MS) {
    lastTxTime = currentTime;

    // Lê potenciômetros e mapeia 0..1023 -> 0..255
    // Leitura sequencial com pequeno delay para ADC estabilizar
    pkt.p[0] = (uint8_t)(analogRead(PIN_POT_0) >> 2);
    pkt.p[1] = (uint8_t)(analogRead(PIN_POT_1) >> 2);
    pkt.p[2] = (uint8_t)(analogRead(PIN_POT_2) >> 2);
    pkt.p[3] = (uint8_t)(analogRead(PIN_POT_3) >> 2);
    pkt.p[4] = (uint8_t)(analogRead(PIN_POT_4) >> 2);
    pkt.p[5] = (uint8_t)(analogRead(PIN_POT_5) >> 2);
    pkt.p[6] = (uint8_t)(analogRead(PIN_POT_6) >> 2);

    // Lê switches (INPUT_PULLUP: LOW quando pressionado -> envia 1)
    pkt.s[0] = (digitalRead(SW1_PIN) == LOW) ? 1 : 0;
    pkt.s[1] = (digitalRead(SW2_PIN) == LOW) ? 1 : 0;
    pkt.s[2] = (digitalRead(SW3_PIN) == LOW) ? 1 : 0;
    pkt.s[3] = (digitalRead(SW4_PIN) == LOW) ? 1 : 0;

    // LED liga durante TX
    digitalWrite(LED_PIN, HIGH);
    
    // Transmite com proteção de energia
    bool txOk = safeTx(&pkt, sizeof(pkt));
    
    // LED desliga após TX
    digitalWrite(LED_PIN, LOW);
    
    // Contadores
    txCount++;
    if (!txOk) txFailCount++;

    // Debug: imprime valores a cada 1 segundo
    static unsigned long lastDebugTime = 0;
    if (currentTime - lastDebugTime >= 1000) {
      lastDebugTime = currentTime;

      // Status da transmissão
      Serial.print(F("TX#"));
      Serial.print(txCount);
      Serial.print(F(" Falhas:"));
      Serial.print(txFailCount);
      Serial.print(F(" ("));
      Serial.print((txFailCount * 100) / txCount);
      Serial.println(F("%)"));
      
      // Valores dos canais
      Serial.print(F("  P["));
      for (uint8_t i = 0; i < 7; i++) {
        if (pkt.p[i] < 100) Serial.print(F(" "));
        if (pkt.p[i] < 10) Serial.print(F(" "));
        Serial.print(pkt.p[i]);
        if (i < 6) Serial.print(F(","));
      }
      Serial.print(F("] SW["));
      for (uint8_t i = 0; i < 4; i++) {
        Serial.print(pkt.s[i]);
        if (i < 3) Serial.print(F(","));
      }
      Serial.println(F("]"));
    }
  }
}
