// ESP8266 HUD – RX do NANO em Serial (GPIO3), 115200 baud
// OLED externa (HW I2C): SCL=D1(GPIO5), SDA=D2(GPIO4)
// OLED interna (SW I2C): SCL=D5(GPIO14), SDA=D6(GPIO12)
// Buzzer via NPN: D0(GPIO16) (HIGH = on)
// Encoder (giro local): CLK=D7(GPIO13), DT=D4(GPIO2)

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <EEPROM.h>

// -------- Displays --------
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2_int(U8G2_R2, D5, D6, U8X8_PIN_NONE);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2_ext(U8G2_R2, /* reset=*/U8X8_PIN_NONE, /* clock=*/D1, /* data=*/D2);

struct Payload;

// -------- Buzzer --------
#define BUZZER_PIN D0
struct BuzzPat{ uint16_t onMs, offMs; uint8_t reps; };
const BuzzPat BZ_CLICK{15,35,1};
const BuzzPat BZ_LONG {55,45,1};
uint8_t  bzLeft=0; uint32_t bzT0=0; uint16_t bzOn=0,bzOff=0; bool bzOnState=false;
void buzz(const BuzzPat&p){ bzOn=p.onMs; bzOff=p.offMs; bzLeft=p.reps*2; bzOnState=true; bzT0=millis(); digitalWrite(BUZZER_PIN,HIGH); }
void buzzerTask(){ if(!bzLeft) return; uint32_t dt=millis()-bzT0; if(bzOnState && dt>=bzOn){ digitalWrite(BUZZER_PIN,LOW); bzOnState=false; bzT0=millis(); bzLeft--; } else if(!bzOnState && dt>=bzOff){ if(bzLeft>1){ digitalWrite(BUZZER_PIN,HIGH); bzOnState=true; bzT0=millis(); bzLeft--; } else { bzLeft=0; } } }

// -------- Protocolo UART do NANO (apenas cliques + status) --------
struct __attribute__((packed)) Payload {
  uint8_t ch[8];     // ignoramos ch[7] (delta), giro é local no ESP
  uint8_t sw;        // bits: 0=S1, 1=S2, 2=CAL
  uint8_t seqShort;  // contador de cliques curtos
  uint8_t seqLong;   // contador de cliques longos
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
  enum {S1,S2,SL,SP,SC}; static uint8_t st=S1,len=0,buf[32],idx=0;
  while (S.available()){
    uint8_t b=S.read();
    switch(st){
      case S1: st=(b==0xAA)?S2:S1; break;
      case S2: st=(b==0x55)?SL:S1; break;
      case SL: len=b; if(len>sizeof(buf)){ st=S1; break; } idx=0; st=SP; break;
      case SP: buf[idx++]=b; if(idx>=len) st=SC; break;
      case SC:{ bool ok=(crc8(buf,len)==b && len==sizeof(Payload)); st=S1; if(ok){ memcpy(&out,buf,sizeof(Payload)); return true; } return false; }
    }
  }
  return false;
}

// -------- Encoder local no ESP: CLK=D7, DT=D4 --------
#define ENC_CLK D7   // GPIO13
#define ENC_DT  D4   // GPIO2 (strap de boot alto)
#define ENC_GATE_US 250UL          // 200–600 us
#define ENC_EDGES_PER_STEP 2       // 2 = 1 passo por “clique” típico (KY-040)

volatile int16_t encEdges = 0;     // soma de transições (+/-)
volatile uint8_t q_prev   = 0;
ICACHE_RAM_ATTR void isrEnc(){
  static uint32_t lastUs=0;
  uint32_t now = micros();
  if (now - lastUs < ENC_GATE_US) return; // suprime bounce
  lastUs = now;

  uint8_t clk = digitalRead(ENC_CLK);
  uint8_t dt  = digitalRead(ENC_DT);
  uint8_t q   = (clk<<1) | dt;

  static const int8_t quad[16] = {
    0, -1, +1,  0,
    +1, 0,  0, -1,
    -1, 0,  0, +1,
     0, +1, -1, 0
  };

  uint8_t idx = (q_prev<<2) | q;
  encEdges += quad[idx];
  q_prev = q;
}

static inline int16_t fetchDetents(){
  static int8_t rem = 0;   // resto de edges não completados
  int16_t edges; noInterrupts(); edges = encEdges; encEdges = 0; interrupts();
  int16_t total = rem + edges;
  int16_t steps = total / ENC_EDGES_PER_STEP;
  rem = total % ENC_EDGES_PER_STEP;
  return steps;            // + horário, - anti-horário
}

// -------- UI/Estados / Config --------
enum Layer : uint8_t { LAYER_EXT=0, LAYER_INT=1 };
enum UiMode : uint8_t { UI_HOME=0, UI_MENU=1, UI_CAL=2 };
Layer  focusLayer = LAYER_EXT;
UiMode uiMode     = UI_HOME;

const char* EXT_CARDS[] = { "Altimetria","Bateria RX","RSSI/LQ","Velocidade","Distancia" };
const char* INT_CARDS[] = { "Bat TX:Nano","Bat TX:ESP","Status Link","Modo/Piloto","Diag" };
const uint8_t EXT_CARD_COUNT = sizeof(EXT_CARDS)/sizeof(EXT_CARDS[0]);
const uint8_t INT_CARD_COUNT = sizeof(INT_CARDS)/sizeof(INT_CARDS[0]);
uint8_t extCard=0, intCard=0;

enum MenuId : uint8_t { M_MAIN=0, M_OPTIONS, M_RADIO };
uint8_t currentMenu = M_MAIN;
int8_t  cursor = 0;

struct Cfg { uint8_t ctrlMode,pilotMode,theme,telemOn,smooth; uint8_t rf_ch,rf_rate,rf_pa,rf_crc,rf_aa,rf_retries; } cfg;
const uint16_t EEPROM_MAGIC = 0xA1C3;
struct Persist { uint16_t magic; Cfg cfg; } persist;
void loadCfg(){ EEPROM.begin(512); EEPROM.get(0,persist); if(persist.magic!=EEPROM_MAGIC){ cfg={0,0,1,1,2,76,1,2,1,1,10}; persist.magic=EEPROM_MAGIC; persist.cfg=cfg; EEPROM.put(0,persist); EEPROM.commit(); } else cfg=persist.cfg; }
void saveCfg(){ persist.magic=EEPROM_MAGIC; persist.cfg=cfg; EEPROM.put(0,persist); EEPROM.commit(); }

const char* CTRL_MODES[]={"Veiculo 1","Veiculo 2","Veiculo 3"};
const char* PILOT_MODES[]={"Responsivo","Alcance","Iniciante","Personal"};
const char* RATES[]={"250K","1M","2M"};
const char* PAs[]  ={"MIN","LOW","HIGH","MAX"};
const char* CRCs[] ={"8b","16b"};

// Payload/estado de link e calibração
Payload cur{}, prev{};
uint8_t prevSeqShort=0, prevSeqLong=0;
uint32_t tLastRx=0; bool linkOk=false;

uint32_t lastMenuInteractionMs=0;
bool menuHelpSticky=true;                   // true ao entrar no menu
const uint16_t MENU_HELP_IDLE_MS = 3000;    // 3 s

bool calSourceNano=false; // true se UI_CAL veio do botão CAL do NANO

// ---- helpers ----
void drawFocusBadge(U8G2& u8,bool f){ if(f) u8.drawBox(122,0,6,6); else u8.drawFrame(122,0,6,6); }
void drawCard(U8G2& u8,const char* title,const char* v1=nullptr,const char* v2=nullptr){
  u8.clearBuffer();
  u8.setFont(u8g2_font_6x13B_tf); u8.drawStr(2,12,title);
  u8.drawHLine(0,14,128);
  u8.setFont(u8g2_font_6x10_tf);
  if(v1)u8.drawStr(2,32,v1);
  if(v2)u8.drawStr(2,48,v2);
  u8.sendBuffer();
}
void drawCentered(U8G2& u8, const char* txt, int y){
  int w = u8.getStrWidth(txt);
  int x = (128 - w) / 2; if (x < 0) x = 0;
  u8.drawStr(x, y, txt);
}
inline void markMenuInteraction(){ lastMenuInteractionMs = millis(); menuHelpSticky = false; }

// ---- Menu textos dinâmicos e navegação ----
void menuReset(){ currentMenu=M_MAIN; cursor=0; }
uint8_t menuItemCount(uint8_t m){ return (m==M_MAIN)?6: (m==M_OPTIONS?6:7); }
void menuItemText(uint8_t m, uint8_t idx, char* out, size_t n){
  if (m==M_MAIN){
    const char* items[] = {"<- Voltar","Modos do controle","Modo de pilotagem","Tema (Claro/Escuro)","Opcoes","Radio (NRF24)"};
    snprintf(out,n,"%s", items[idx]);
  } else if (m==M_OPTIONS){
    switch(idx){
      case 0: snprintf(out,n,"<- Voltar"); break;
      case 1: snprintf(out,n,"Telemetria: %s", cfg.telemOn?"On":"Off"); break;
      case 2: snprintf(out,n,"Smooth: %u", cfg.smooth); break;
      case 3: snprintf(out,n,"Calibracao"); break;          // <— rótulo curtinho
      case 4: snprintf(out,n,"Salvar cfg (auto)"); break;
      case 5: snprintf(out,n,"Resetar cfg"); break;
    }
  } else { // M_RADIO
    switch(idx){
      case 0: snprintf(out,n,"<- Voltar"); break;
      case 1: snprintf(out,n,"Canal RF: %u", cfg.rf_ch); break;
      case 2: snprintf(out,n,"DataRate: %s", RATES[cfg.rf_rate]); break;
      case 3: snprintf(out,n,"PA: %s", PAs[cfg.rf_pa]); break;
      case 4: snprintf(out,n,"CRC: %s", CRCs[cfg.rf_crc]); break;
      case 5: snprintf(out,n,"AutoAck: %s", cfg.rf_aa?"On":"Off"); break;
      case 6: snprintf(out,n,"Retries: %u", cfg.rf_retries); break;
    }
  }
}
void menuScroll(int8_t dir){
  int8_t maxIdx = menuItemCount(currentMenu)-1;
  cursor += dir; if (cursor<0) cursor=maxIdx; if (cursor>maxIdx) cursor=0;
}
void menuChangeValue(int8_t step){
  if(currentMenu==M_MAIN){
    if      (cursor==1){ cfg.ctrlMode =(cfg.ctrlMode +step+3)%3; saveCfg(); }
    else if (cursor==2){ cfg.pilotMode=(cfg.pilotMode+step+4)%4; saveCfg(); }
    else if (cursor==3){ cfg.theme    =(cfg.theme    +step+2)%2; saveCfg(); }
  }else if(currentMenu==M_OPTIONS){
    if      (cursor==1){ cfg.telemOn^=1; saveCfg(); }
    else if (cursor==2){ int v=cfg.smooth+step; if(v<0)v=5; if(v>5)v=0; cfg.smooth=v; saveCfg(); }
    else if (cursor==5){ Cfg keep=cfg; keep.ctrlMode=0; keep.pilotMode=0; keep.telemOn=1; keep.smooth=2; keep.rf_ch=76; keep.rf_rate=1; keep.rf_pa=2; keep.rf_crc=1; keep.rf_aa=1; keep.rf_retries=10; keep.theme=cfg.theme; cfg=keep; saveCfg(); }
  }else if(currentMenu==M_RADIO){
    if      (cursor==1){ int ch=cfg.rf_ch+step; if(ch<0)ch=125; if(ch>125)ch=0; cfg.rf_ch=ch; saveCfg(); }
    else if (cursor==2){ cfg.rf_rate=(cfg.rf_rate+step+3)%3; saveCfg(); }
    else if (cursor==3){ cfg.rf_pa  =(cfg.rf_pa  +step+4)%4; saveCfg(); }
    else if (cursor==4){ cfg.rf_crc^=1; saveCfg(); }
    else if (cursor==5){ cfg.rf_aa ^=1; saveCfg(); }
    else if (cursor==6){ int rr=cfg.rf_retries+step; if(rr<0)rr=15; if(rr>15)rr=0; cfg.rf_retries=rr; saveCfg(); }
  }
}
void menuEnter(){
  if(currentMenu==M_MAIN){
    if      (cursor==0) uiMode=UI_HOME;
    else if (cursor==4){ currentMenu=M_OPTIONS; cursor=0; }
    else if (cursor==5){ currentMenu=M_RADIO;   cursor=0; }
    // itens 1..3 ajustam via scroll
  } else if(currentMenu==M_OPTIONS){
    if (cursor==0){ currentMenu=M_MAIN; cursor=0; }
    else if (cursor==3){
      // abrir tela de calibração a partir do menu
      uiMode = UI_CAL; calSourceNano = false;
    }
  } else { // M_RADIO
    if (cursor==0){ currentMenu=M_MAIN; cursor=0; }
  }
}
void onShortClick(){
  if(uiMode==UI_HOME){
    focusLayer=(focusLayer==LAYER_EXT)?LAYER_INT:LAYER_EXT;
  } else if (uiMode==UI_MENU){
    menuEnter(); markMenuInteraction();
  } else if (uiMode==UI_CAL){
    // sem ação no short (mantemos tela "pura" sem texto)
  }
}
void onLongPress(){
  if (uiMode==UI_HOME){
    uiMode=UI_MENU; menuReset();
    menuHelpSticky = true;              // ao abrir, mostra instruções
    lastMenuInteractionMs = millis();
  } else if (uiMode==UI_MENU){
    uiMode=UI_HOME;
  } else if (uiMode==UI_CAL){
    // só sai por long se entrou via menu; se veio do NANO, sai quando CAL soltar
    if (!calSourceNano) uiMode = UI_MENU;
  }
}

// ---- Renders HOME / MENU / CAL ----
void renderHome(){
  const char* intTitle=INT_CARDS[intCard]; const char* extTitle=EXT_CARDS[extCard];
  // externa
  u8g2_ext.clearBuffer(); drawFocusBadge(u8g2_ext, focusLayer==LAYER_EXT);
  if      (strcmp(extTitle,"Altimetria")==0)  drawCard(u8g2_ext,"Altimetria","Aguardando...","---");
  else if (strcmp(extTitle,"Bateria RX")==0)  drawCard(u8g2_ext,"Bateria RX","V: N/A","%: N/A");
  else if (strcmp(extTitle,"RSSI/LQ")==0)     drawCard(u8g2_ext,"RSSI/LQ","RSSI: N/A","LQ: N/A");
  else if (strcmp(extTitle,"Velocidade")==0)  drawCard(u8g2_ext,"Velocidade","N/A",nullptr);
  else if (strcmp(extTitle,"Distancia")==0)   drawCard(u8g2_ext,"Distancia","N/A",nullptr);
  u8g2_ext.sendBuffer();
  // interna
  u8g2_int.clearBuffer(); drawFocusBadge(u8g2_int, focusLayer==LAYER_INT);
  if      (strcmp(intTitle,"Bat TX:Nano")==0) drawCard(u8g2_int,"Bat TX:Nano","V: N/A","%: N/A");
  else if (strcmp(intTitle,"Bat TX:ESP")==0)  drawCard(u8g2_int,"Bat TX:ESP","V: N/A","%: N/A");
  else if (strcmp(intTitle,"Status Link")==0) drawCard(u8g2_int,"Status Link", linkOk? "OK":"Sem sinal", nullptr);
  else if (strcmp(intTitle,"Modo/Piloto")==0){ char l1[24]; snprintf(l1,sizeof(l1),"Ctrl: %u",(unsigned)(cfg.ctrlMode+1)); const char* pm=PILOT_MODES[cfg.pilotMode%4]; char l2[24]; snprintf(l2,sizeof(l2),"Pilot: %s",pm); drawCard(u8g2_int,"Modo/Piloto",l1,l2); }
  else if (strcmp(intTitle,"Diag")==0){ char l1[24]; snprintf(l1,sizeof(l1),"CH:%u RATE:%s",cfg.rf_ch,RATES[cfg.rf_rate]); char l2[24]; snprintf(l2,sizeof(l2),"PA:%s CRC:%s",PAs[cfg.rf_pa],CRCs[cfg.rf_crc]); drawCard(u8g2_int,"Diag",l1,l2); }
  u8g2_int.sendBuffer();
}

void renderMenu(){
  // externa
  bool idle = (millis() - lastMenuInteractionMs) >= MENU_HELP_IDLE_MS;
  bool showHelp = menuHelpSticky || idle;
  u8g2_ext.clearBuffer();
  if (showHelp){
    u8g2_ext.setFont(u8g2_font_7x14B_tf); drawCentered(u8g2_ext, "MENU ATIVO", 22);
    u8g2_ext.setFont(u8g2_font_6x10_tf);  drawCentered(u8g2_ext, "Gire p/ navegar", 40);
    drawCentered(u8g2_ext, "Clique curto = Enter", 54);
  } else {
    char sel[28]; menuItemText(currentMenu, cursor, sel, sizeof(sel));
    u8g2_ext.setFont(u8g2_font_7x14B_tf); drawCentered(u8g2_ext, sel, 40);
  }
  u8g2_ext.sendBuffer();

  // interna (menu circular 3 itens: prev/next + faixa inferior selecionada)
  const uint8_t bandH = 16, bandY = 64 - bandH;
  uint8_t count = menuItemCount(currentMenu);
  uint8_t prevIdx = (cursor + count - 1) % count;
  uint8_t nextIdx = (cursor + 1) % count;

  char prevT[28], selT[28], nextT[28];
  menuItemText(currentMenu, prevIdx, prevT, sizeof(prevT));
  menuItemText(currentMenu, cursor , selT , sizeof(selT));
  menuItemText(currentMenu, nextIdx, nextT, sizeof(nextT));

  u8g2_int.clearBuffer();
  u8g2_int.setFont(u8g2_font_6x10_tf);
  drawCentered(u8g2_int, prevT, 22);
  drawCentered(u8g2_int, nextT, 38);
  u8g2_int.drawBox(0, bandY, 128, bandH);
  u8g2_int.setDrawColor(0);
  u8g2_int.setFont(u8g2_font_6x13B_tf);
  drawCentered(u8g2_int, selT, bandY + 12);
  u8g2_int.setDrawColor(1);
  u8g2_int.sendBuffer();
}

// ---- Calibração: 8 barras + 2 círculos (sem texto), em AMBAS as telas ----
static inline uint8_t map255(uint8_t v, uint8_t span){ return (uint16_t)v * span / 255; }

void renderCalibration(){
  // parâmetros visuais
  const uint8_t top = 2;
  const uint8_t barH = 44;           // altura útil das barras
  const uint8_t baseY = top + barH;  // linha de base das barras
  const uint8_t barW = 12;
  const uint8_t gap  = 4;
  const uint8_t totalW = 8*barW + 7*gap;     // 124 px
  const uint8_t x0 = (128 - totalW)/2;       // centraliza → 2 px
  const uint8_t r = 5;                       // raio dos círculos
  const uint8_t cy = 62;                     // y dos círculos (quase no rodapé)
  const uint8_t c1x = 128/2 - 24;            // posições x dos círculos
  const uint8_t c2x = 128/2 + 24;

  auto drawOne = [&](U8G2& d){
    d.clearBuffer();

    // 8 barras verticais sem texto
    for (uint8_t i=0;i<8;i++){
      uint8_t x = x0 + i*(barW+gap);
      d.drawFrame(x, top, barW, barH);
      uint8_t fill = map255(cur.ch[i], barH-2);
      // enche de baixo pra cima
      if (fill>0) d.drawBox(x+1, baseY - fill, barW-2, fill);
    }

    // círculos dos switches S1/S2
    bool s1 = cur.sw & 0x01;
    bool s2 = cur.sw & 0x02;
    if (s1) d.drawDisc(c1x, cy, r); else d.drawCircle(c1x, cy, r);
    if (s2) d.drawDisc(c2x, cy, r); else d.drawCircle(c2x, cy, r);

    d.sendBuffer();
  };

  drawOne(u8g2_ext);
  drawOne(u8g2_int);
}

// ---- Aplicar passos vindos do encoder local ----
void applyDeltaSteps(int16_t steps){
  if(steps==0) return;
  int8_t dir = (steps>0)? +1 : -1;
  for(int16_t k=0;k<abs(steps);k++){
    if(uiMode==UI_HOME){
      if (focusLayer==LAYER_EXT) extCard=(extCard+(dir>0?1:-1)+EXT_CARD_COUNT)%EXT_CARD_COUNT;
      else                       intCard=(intCard+(dir>0?1:-1)+INT_CARD_COUNT)%INT_CARD_COUNT;
    } else if (uiMode==UI_MENU){
      menuScroll(dir>0?1:-1);
      menuChangeValue(dir>0?+1:-1);
      markMenuInteraction();
    } else if (uiMode==UI_CAL){
      // sem ação: tela de calibração é só visual
    }
  }
}

void setup(){
  pinMode(BUZZER_PIN, OUTPUT); digitalWrite(BUZZER_PIN, LOW);
  Serial.begin(115200);

  // Encoder local
  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT , INPUT_PULLUP);
  q_prev = (digitalRead(ENC_CLK)<<1) | digitalRead(ENC_DT);
  attachInterrupt(digitalPinToInterrupt(ENC_CLK), isrEnc, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_DT ), isrEnc, CHANGE);

  u8g2_int.begin(); u8g2_ext.begin();
  loadCfg();

  // Splash
  u8g2_ext.clearBuffer(); u8g2_ext.setFont(u8g2_font_6x13B_tf); u8g2_ext.drawStr(6,28,"OpenRC AeroLink"); u8g2_ext.drawStr(6,44,"HUD ESP8266"); u8g2_ext.sendBuffer();
  u8g2_int.clearBuffer(); u8g2_int.setFont(u8g2_font_6x10_tf); u8g2_int.drawStr(8,32,"Aguardando link..."); u8g2_int.sendBuffer();
}

void loop(){
  buzzerTask();

  // RX do NANO (somente cliques e status)
  Payload rx; bool got = readFrame(Serial, rx);
  if (got){ cur=rx; tLastRx=millis(); }

  // detectar CAL do NANO e entrar/sair da Ui de calibração automaticamente
  bool calNow = cur.sw & 0x04;
  static bool calPrev = false;
  if (calNow && !calPrev){
    uiMode = UI_CAL; calSourceNano = true;
  } else if (!calNow && calPrev){
    if (uiMode==UI_CAL && calSourceNano) uiMode = UI_HOME;
    calSourceNano = false;
  }
  calPrev = calNow;

  linkOk = (millis()-tLastRx) < 300;

  // Eventos por contadores (curtos/longos)
  uint8_t dShort = cur.seqShort - prevSeqShort;
  uint8_t dLong  = cur.seqLong  - prevSeqLong;

  if (dShort && uiMode!=UI_CAL){ // na tela de calibração, short não faz nada
    for(uint8_t i=0;i<dShort;i++){ onShortClick(); buzz(BZ_CLICK); }
    prevSeqShort=cur.seqShort;
  } else if (dShort){
    prevSeqShort=cur.seqShort; // consome mesmo assim
  }

  if (dLong){
    for(uint8_t i=0;i<dLong;i++){
      // long press fecha menu, abre menu, ou (se UI_CAL via menu) sai da calibração
      onLongPress(); buzz(BZ_LONG);
    }
    prevSeqLong=cur.seqLong;
  }

  // Passos do encoder local
  int16_t det = fetchDetents();
  if (det) applyDeltaSteps(det);

  // Render ~30 FPS
  static uint32_t tDraw=0;
  if (millis()-tDraw >= 33){
    tDraw=millis();
    if      (uiMode==UI_HOME) renderHome();
    else if (uiMode==UI_MENU) renderMenu();
    else                      renderCalibration(); // UI_CAL
  }
}
