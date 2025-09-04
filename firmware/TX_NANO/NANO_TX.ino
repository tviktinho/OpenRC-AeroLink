/* NANO_TX — Encoder 1 edge = 1 passo + long/short estáveis
   Ligações:
     NRF24: CE=D8, CSN=D7 (SPI padrão)
     Pots: A0..A7
     Encoder: CLK=D2 (INT0), DT=D3, SW=D10 (INPUT_PULLUP)
     Switches: SW1=D5, SW2=D4, CAL=D6 (INPUT_PULLUP)
     UART -> ESP: TX0(D1) -> divisor (~3k3/1k8) -> RX0(ESP). 38400 baud.

   UART frame: AA 55 | len(11) | {ch[8], sw, seqShort, seqLong} | crc8(0x8C)
   ch[7] = delta do encoder em passos (int8 reempacotado: (int8)+128)
   sw bits: 0=S1, 1=S2, 2=CAL
*/

#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

#define LIVE_BAUD 38400

// RF (opcional, igual antes)
#define CE_PIN 8
#define CSN_PIN 7
RF24 radio(CE_PIN, CSN_PIN);
const byte ADDRESS[6] = "00001";

// Entradas
const uint8_t potPins[8] = {A0,A1,A2,A3,A4,A5,A6,A7};
#define SW1_PIN        5
#define SW2_PIN        4
#define CALIB_BTN_PIN  6

// Encoder
#define ENC_CLK_PIN 2   // INT0
#define ENC_DT_PIN  3
#define ENC_SW_PIN 10

// Ajustes do encoder
#define ENC_GATE_US 800UL  // tempo mínimo entre bordas (600–1200 se ainda “salta”)

// Suavização dos pots
const uint8_t SMOOTHING = 2;

// Pacotes
struct PacketRF { uint8_t p[8]; uint8_t s1; uint8_t s2; } pkt;
struct __attribute__((packed)) PayloadUART {
  uint8_t ch[8];
  uint8_t sw;
  uint8_t seqShort;
  uint8_t seqLong;
} upkt;

struct Calib { uint16_t minRaw[8]; uint16_t maxRaw[8]; } calib;
uint8_t  smoothVal[8] = {0};
bool     calibrating = false;
uint32_t tSend=0;

// ---------- Encoder: 1 ISR no CLK + gating temporal ----------
volatile int16_t encSteps = 0;  // já em "passos" (±1 por borda válida)

void ISR_CLK(){
  static uint32_t lastUs = 0;
  uint32_t now = micros();
  if (now - lastUs < ENC_GATE_US) return;   // gating temporal contra bounce
  lastUs = now;

  // Direção pela leitura do DT no mesmo instante
  int dir = digitalRead(ENC_DT_PIN) ? +1 : -1;
  encSteps += dir;                           // 1 edge = 1 passo
}

// ---------- Botão do encoder: 1 long por pressão (latch) ----------
enum BtnSt { BTN_UP, BTN_DOWN, BTN_LONG_LATCH };
const unsigned BTN_DEBOUNCE_MS = 30;
const unsigned BTN_LONG_MS     = 650;
BtnSt   btnSt = BTN_UP;
bool    rawPrev = true;            // pull-up → true solto
bool    stable  = true;
uint32_t tLastChange = 0;
uint32_t tPressStart = 0;
uint8_t  seqClickShort = 0;
uint8_t  seqClickLong  = 0;

inline void pollEncButton(){
  bool raw = digitalRead(ENC_SW_PIN); // HIGH=solto, LOW=pressionado
  uint32_t ms = millis();

  if (raw != rawPrev){ rawPrev = raw; tLastChange = ms; }
  bool debounced = (ms - tLastChange) >= BTN_DEBOUNCE_MS;
  if (!debounced) return;

  if (stable != raw){
    stable = raw;
    if (stable == LOW){ // DOWN
      tPressStart = ms;
      if (btnSt == BTN_UP) btnSt = BTN_DOWN;
    } else { // UP
      if (btnSt == BTN_DOWN){
        uint32_t dur = ms - tPressStart;
        if (dur >= 40 && dur < BTN_LONG_MS) seqClickShort++;
      }
      btnSt = BTN_UP; // soltar sempre sai do latch
    }
  }

  if (btnSt == BTN_DOWN && stable == LOW){
    if (ms - tPressStart >= BTN_LONG_MS){
      seqClickLong++;
      btnSt = BTN_LONG_LATCH; // trava até soltar
    }
  }
}

// ---------- CRC8 ----------
static uint8_t crc8(const uint8_t* d, size_t n){
  uint8_t c=0x00;
  for(size_t i=0;i<n;i++){
    uint8_t x=d[i];
    for(uint8_t b=0;b<8;b++){ uint8_t mix=(c^x)&1; c>>=1; if(mix) c^=0x8C; x>>=1; }
  }
  return c;
}

// ---------- Calibração ----------
inline void resetCalibrationBounds(){ for(uint8_t i=0;i<8;i++){ calib.minRaw[i]=1023; calib.maxRaw[i]=0; } }
inline void finalizeCalibrationBounds(){ for(uint8_t i=0;i<8;i++){ if (calib.maxRaw[i]<=calib.minRaw[i] || (calib.maxRaw[i]-calib.minRaw[i] < 8)){ calib.minRaw[i]=0; calib.maxRaw[i]=1023; } } }
inline void updateCalibrationBoundsOnce(){ for(uint8_t i=0;i<8;i++){ int r=analogRead(potPins[i]); if(r<calib.minRaw[i])calib.minRaw[i]=r; if(r>calib.maxRaw[i])calib.maxRaw[i]=r; } }
static inline uint8_t normalizeToByte(int raw, uint16_t mn, uint16_t mx){
  if (mx<=mn){ mn=0; mx=1023; }
  if (raw < (int)mn) raw = mn; if (raw > (int)mx) raw = mx;
  return (uint8_t)map(raw, mn, mx, 0, 255);
}

// ---------- Rádio ----------
static bool initRadio(){
  delay(50);
  if(!radio.begin()) return false;
  pinMode(10, OUTPUT);
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_250KBPS);
  radio.setChannel(76);
  radio.setAutoAck(false);
  radio.openWritingPipe(ADDRESS);
  radio.stopListening();
  return radio.isChipConnected();
}

void setup(){
  Serial.begin(LIVE_BAUD);

  pinMode(SW1_PIN, INPUT_PULLUP);
  pinMode(SW2_PIN, INPUT_PULLUP);
  pinMode(CALIB_BTN_PIN, INPUT_PULLUP);

  pinMode(ENC_CLK_PIN, INPUT_PULLUP);
  pinMode(ENC_DT_PIN,  INPUT_PULLUP);
  pinMode(ENC_SW_PIN,  INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(ENC_CLK_PIN), ISR_CLK, RISING); // 1 ISR só

  initRadio();

  resetCalibrationBounds();
  for (uint8_t i=0;i<8;i++){
    int raw = analogRead(potPins[i]);
    smoothVal[i] = normalizeToByte(raw, 0, 1023);
  }

  tSend = millis();
}

void loop(){
  pollEncButton();

  // CAL
  const bool calPressed = (digitalRead(CALIB_BTN_PIN) == LOW);
  static bool calPrev=false;
  if (calPressed && !calPrev){ calibrating=true;  resetCalibrationBounds(); }
  if (!calPressed &&  calPrev){ calibrating=false; finalizeCalibrationBounds(); }
  calPrev = calPressed;
  if (calibrating) updateCalibrationBoundsOnce();

  uint32_t now = millis();

  // 100 Hz (10 ms) para reduzir latência
  if (now - tSend >= 10){
    tSend = now;

    // Canais
    for (uint8_t i=0;i<8;i++){
      int raw = analogRead(potPins[i]);
      uint16_t mn = calibrating ? 0 : calib.minRaw[i];
      uint16_t mx = calibrating ? 1023 : calib.maxRaw[i];
      uint8_t nrm = normalizeToByte(raw, mn, mx);
      smoothVal[i] = smoothVal[i] + (int)(nrm - smoothVal[i]) / (SMOOTHING + 1);
      pkt.p[i] = smoothVal[i];
    }

    // Switches
    pkt.s1 = (digitalRead(SW1_PIN) == LOW);
    pkt.s2 = (digitalRead(SW2_PIN) == LOW);

    // RF (opcional)
    radio.write(&pkt, sizeof(pkt));

    // Payload UART
    for (uint8_t i=0;i<8;i++) upkt.ch[i] = pkt.p[i];

    // Pega passos acumulados (1 edge = 1 passo)
    int16_t steps;
    noInterrupts(); steps = encSteps; encSteps = 0; interrupts();

    // Satura em int8
    if (steps >  31) steps =  31;
    if (steps < -31) steps = -31;

    upkt.ch[7] = (int8_t)steps + 128;

    // Bits e contadores
    uint8_t sw = 0;
    if (pkt.s1)     sw |= (1<<0);
    if (pkt.s2)     sw |= (1<<1);
    if (calPressed) sw |= (1<<2);
    upkt.sw = sw;

    upkt.seqShort = seqClickShort;
    upkt.seqLong  = seqClickLong;

    const uint8_t* pay = (const uint8_t*)&upkt;
    const uint8_t  LEN = sizeof(PayloadUART);
    const uint8_t  CRC = crc8(pay, LEN);

    Serial.write(0xAA); Serial.write(0x55); Serial.write(LEN);
    Serial.write(pay, LEN); Serial.write(CRC);
  }
}
