// ESP8266 HUD duplo lendo NANO_TX por UART (via SoftwareSerial no D3 @ 38400)
// INT (superior): SCL=D5, SDA=D6, U8G2_R0  -> Mostra título "OpenRC AeroLink" no boot
// EXT (inferior): SCL=D1, SDA=D2, U8G2_R2  -> Barra de progresso + animação de loading
// Buzzer ativo 3V3: D0 (GPIO16) - blip curto ao fim do boot e bipes em trocas de modo (CAL<->NORMAL)
// Animação CAL: ponto girando (overlay leve)
//
// >>> ATENÇÃO: Configure o NANO para transmitir a 38400 bps <<<
// TX do NANO -> (divisor 5V->3V3) -> D3 (GPIO0) do ESP8266
// GND comum entre NANO e ESP. Evite tráfego durante o boot do ESP (GPIO0 deve ficar alto).

#include <Arduino.h>
#include <U8g2lib.h>
#include <stdint.h>
#include <SoftwareSerial.h>

#define LOGO_WIDTH  128
#define LOGO_HEIGHT 64
#define BUZZER_PIN  D7   // buzzer em D0 (GPIO16)

// UART do NANO "escutada" no D3 (GPIO0). Apenas RX (TX=-1), lógica não invertida.
SoftwareSerial NanoUart(D3, -1, false);

// --- corrige o autoprotótipo do Arduino ---
struct BuzzPat;                 // forward declaration do tipo
void startBuzz(const BuzzPat&); // protótipo manual (bloqueia o autoproto)

// -------- Displays --------
U8G2_SSD1306_128X64_NONAME_F_SW_I2C oledInt(
  U8G2_R2, /*clock=*/D5, /*data=*/D6, /*reset=*/U8X8_PIN_NONE
);
U8G2_SSD1306_128X64_NONAME_F_SW_I2C oledExt(
  U8G2_R2, /*clock=*/D1, /*data=*/D2, /*reset=*/U8X8_PIN_NONE
);

// -------- Payload UART --------
struct __attribute__((packed)) Payload {
  uint8_t ch[8];       // 0..255
  uint8_t sw;          // bit0=S1, bit1=S2, bit2=CAL
  uint8_t rfu0, rfu1;  // 0
};

static uint8_t crc8(const uint8_t* d, size_t n){
  uint8_t c=0x00;
  for(size_t i=0;i<n;i++){
    uint8_t x=d[i];
    for(uint8_t b=0;b<8;b++){ uint8_t mix=(c^x)&1; c>>=1; if(mix) c^=0x8C; x>>=1; }
  }
  return c;
}

bool readFrame(Stream &S, Payload &out){
  enum {S1,S2,SL,SP,SC};
  static uint8_t st=S1, len=0, buf[32], idx=0;
  while (S.available()){
    uint8_t b = S.read();
    switch(st){
      case S1: st=(b==0xAA)?S2:S1; break;
      case S2: st=(b==0x55)?SL:S1; break;
      case SL: len=b; if(len>sizeof(buf)){ st=S1; break; } idx=0; st=SP; break;
      case SP: buf[idx++]=b; if(idx>=len) st=SC; break;
      case SC:{
        uint8_t c=crc8(buf,len);
        bool ok=(c==b && len==sizeof(Payload));
        st=S1;
        if (ok){ memcpy(&out,buf,sizeof(Payload)); return true; }
        return false;
      }
    }
  }
  return false;
}

// -------- Helpers de desenho --------
static inline uint8_t barW(uint8_t v, uint8_t w){ return (uint16_t(v)*(w-2)+127)/255; }

// -------- Buzzer (não-bloqueante) --------
enum BuzzState { BZ_IDLE, BZ_ON, BZ_OFF };

struct BuzzPat {
  uint16_t onMs;
  uint16_t offMs;
  uint8_t  reps;
};

// (variáveis do buzzer)
BuzzState bzState = BZ_IDLE;
uint32_t  bzT0 = 0;
uint8_t   bzLeft = 0;
uint16_t  bzOnMs=0, bzOffMs=0;

void startBuzz(const BuzzPat &p){
  bzOnMs = p.onMs;
  bzOffMs = p.offMs;
  bzLeft = p.reps * 2;   // ON+OFF = 2 steps
  bzState = BZ_ON;
  bzT0 = millis();
  digitalWrite(BUZZER_PIN, HIGH);
}

void buzzerTask(){
  if (bzState==BZ_IDLE || bzLeft==0) return;
  uint32_t dt = millis() - bzT0;
  if (bzState==BZ_ON && dt >= bzOnMs){
    digitalWrite(BUZZER_PIN, LOW);
    bzState = BZ_OFF; bzT0 = millis(); bzLeft--;
  } else if (bzState==BZ_OFF && dt >= bzOffMs){
    if (bzLeft>1){
      digitalWrite(BUZZER_PIN, HIGH);
      bzState = BZ_ON;  bzT0 = millis(); bzLeft--;
    } else {
      bzState = BZ_IDLE; bzLeft = 0; digitalWrite(BUZZER_PIN, LOW);
    }
  }
}

const BuzzPat BZ_EXIT  = {120, 120, 2};  // bi-bi (sair de CAL)
const BuzzPat BZ_ENTER = {350, 120, 1};  // biiii (entrar em CAL)
const BuzzPat BZ_BOOT  = {10,  40,  1};  // blip curto ao fim do boot

// -------- Animação CAL / Loading (ponto girando) --------
const int8_t CIRC_X[16] = { 0, 3, 6, 8,  9,  8,  6, 3,  0, -3, -6, -8, -9, -8, -6, -3 };
const int8_t CIRC_Y[16] = {-9,-8,-6,-3,  0,  3,  6, 8,  9,  8,  6,  3,  0, -3, -6, -8 };

void drawSpin(U8G2 &d, int cx, int cy, uint16_t msPerStep=60){
  static uint8_t step=0;
  step = (millis()/msPerStep) & 0x0F; // ~16 fps
  d.drawCircle(cx, cy, 11, U8G2_DRAW_ALL);
  d.drawDisc  (cx + CIRC_X[step], cy + CIRC_Y[step], 2, U8G2_DRAW_ALL);
}

// -------- Telas --------
void drawIntegrated(U8G2 &d, const Payload& p, bool linkOk, bool cal){
  d.clearBuffer();
  d.setFont(u8g2_font_6x10_tf);
  d.drawStr(0,10,"INT C1..C4 + dot C5..C8");
  d.drawStr(94,10, linkOk ? "LINK" : "----");

  const int w = 60, h = 10, dx = 64, dy = 22;
  for(int i=0;i<4;i++){
    int col = i/2, row = i%2;
    int x = 4 + col*dx;
    int y = 16 + row*dy;
    d.drawFrame(x,y,w,h);
    uint8_t bw = barW(p.ch[i], w);
    d.drawBox(x+1, y+1, bw, h-2);

    uint8_t bx = x+1 + barW(p.ch[4+i], w);
    int cy = y + h/2;
    d.drawDisc(bx, cy, 2, U8G2_DRAW_ALL);

    d.setCursor(x, y+h+10);
    d.print('C'); d.print(i+1); d.print('/'); d.print(i+5);
    d.print(" v="); d.print((int)p.ch[i]);
  }

  if (cal){
    d.setCursor(98, 24); d.print("CAL");
    drawSpin(d, 64, 32);
  }
  d.sendBuffer();
}

void drawExternal(U8G2 &d, const Payload& p, bool linkOk, bool cal){
  d.clearBuffer();
  d.setFont(u8g2_font_6x10_tf);
  d.drawStr(94,10, linkOk ? "LINK" : "----");

  const int w = 60, h = 10, dx = 64, dy = 22;
  for(int i=0;i<4;i++){
    int col = i/2, row = i%2;
    int x = 4 + col*dx;
    int y = 16 + row*dy;
    d.drawFrame(x,y,w,h);
    uint8_t bw = barW(p.ch[i], w);
    d.drawBox(x+1, y+1, bw, h-2);

    uint8_t bx = x+1 + barW(p.ch[4+i], w);
    int cy = y + h/2;
    d.drawDisc(bx, cy, 2, U8G2_DRAW_ALL);
  }

  // S1/S2/CAL destaque
  d.setFont(u8g2_font_7x14B_tf);
  d.drawStr(4, 62,  (p.sw&1)?"ON ":"OFF");
  d.drawStr(68,62,  (p.sw&2)?"ON ":"OFF");

  if (cal) drawSpin(d, 64, 32);
  d.sendBuffer();
}

// -------- Estado --------
Payload cur{}, prev{};
uint32_t tLastRx=0;
bool calPrev=false;

static bool payloadChanged(const Payload& a, const Payload& b){
  if (a.sw != b.sw) return true;
  for (int i=0;i<8;i++) if (a.ch[i]!=b.ch[i]) return true;
  return false;
}

// -------- Boot Splash (sem bitmap): título em cima, barra+animação embaixo --------
void bootSplash(uint16_t total_ms = 5000) {
  uint32_t t0 = millis();
  uint8_t dotCount = 0;

  while (true) {
    uint32_t elapsed = millis() - t0;
    if (elapsed > total_ms) elapsed = total_ms;

    // calcula progresso da barra
    const int BAR_X = 8, BAR_W = 112, BAR_H = 8, BAR_Y = 64 - 10;
    uint16_t fill = (uint32_t)elapsed * (BAR_W - 2) / total_ms;

     // --- Tela de cima (nome do projeto) ---
    oledExt.clearBuffer();
    oledExt.setFont(u8g2_font_ncenB14_tr);
    oledExt.drawStr(20, 32, "OpenRC");
    oledExt.drawStr(18, 52, "AeroLink");
    oledExt.sendBuffer();

    // --- Tela de baixo (barra + pontos pulando) ---
    oledInt.clearBuffer();
    oledInt.setFont(u8g2_font_6x10_tf);
    oledInt.drawFrame(BAR_X, BAR_Y, BAR_W, BAR_H);
    oledInt.drawBox(BAR_X + 1, BAR_Y + 1, fill, BAR_H - 2);

    // animação de pontos (muda a cada ~300ms)
    dotCount = (elapsed / 300) % 4; // 0,1,2,3
    String dots = "";
    for (uint8_t i = 0; i < dotCount; i++) dots += ".";
    oledInt.setCursor(30, 40);
    oledInt.print("Loading");
    oledInt.print(dots);
    oledInt.sendBuffer();

    if (elapsed >= total_ms) break;
    delay(50);
    yield();
  }

  // ao terminar → beep curto
  digitalWrite(BUZZER_PIN, HIGH);
  delay(50); // blip bem curto
  digitalWrite(BUZZER_PIN, LOW);
}


void setup(){
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Logs na USB
  Serial.begin(115200);

  // UART do NANO via SoftwareSerial no D3 (GPIO0). NANO deve transmitir a 38400.
  NanoUart.begin(38400);

  oledInt.begin();
  oledExt.begin();

  // Splash de boot customizado
  bootSplash(5000);
  delay(100);
}

void loop(){
  bool got = readFrame(NanoUart, cur);
  if (got) tLastRx = millis();

  if (got) {
    Serial.print("[RX] ch0="); Serial.print(cur.ch[0]);
    Serial.print(" sw="); Serial.println(cur.sw, BIN);
  }

  // Buzzer em trocas de modo CAL
  bool calNow = (cur.sw & 0x04);
  if (calNow != calPrev){
    if (calNow) startBuzz(BZ_ENTER); else startBuzz(BZ_EXIT);
    calPrev = calNow;
  }
  buzzerTask();

  // Nano transmite ~a cada 25 ms; 300 ms é margem segura para "link ok"
  bool linkOk = (millis() - tLastRx) < 300;

  static uint32_t tDraw=0;
  bool needDraw = got || payloadChanged(cur, prev) || (millis()-tDraw)>=33; // ~30 FPS

  if (needDraw){
    tDraw = millis();
    drawIntegrated(oledInt, cur, linkOk, calNow);
    drawExternal  (oledExt, cur, linkOk, calNow);
    prev = cur;
  }
}
