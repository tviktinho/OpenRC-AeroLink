// ESP8266 HUD – RX do NANO em Serial (GPIO3), 38400 baud
// OLED externa (HW I2C): SCL=D1(GPIO5), SDA=D2(GPIO4)
// OLED interna (SW I2C): SCL=D5(GPIO14), SDA=D6(GPIO12)
// Buzzer via NPN: D0(GPIO16) (HIGH = on)

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <EEPROM.h>

U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2_int(U8G2_R2, D5, D6, U8X8_PIN_NONE);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2_ext(U8G2_R2, /* reset=*/U8X8_PIN_NONE, /* clock=*/D1, /* data=*/D2);

struct Payload;                                     // <-- importante
bool readFrame(Stream &S, Payload &out); 

#define BUZZER_PIN D0
struct BuzzPat{ uint16_t onMs, offMs; uint8_t reps; };
const BuzzPat BZ_CLICK{15,35,1};
const BuzzPat BZ_LONG {55,45,1};
uint8_t  bzLeft=0; uint32_t bzT0=0; uint16_t bzOn=0,bzOff=0; bool bzOnState=false;
void buzz(const BuzzPat&p){ bzOn=p.onMs; bzOff=p.offMs; bzLeft=p.reps*2; bzOnState=true; bzT0=millis(); digitalWrite(BUZZER_PIN,HIGH); }
void buzzerTask(){ if(!bzLeft) return; uint32_t dt=millis()-bzT0; if(bzOnState && dt>=bzOn){ digitalWrite(BUZZER_PIN,LOW); bzOnState=false; bzT0=millis(); bzLeft--; } else if(!bzOnState && dt>=bzOff){ if(bzLeft>1){ digitalWrite(BUZZER_PIN,HIGH); bzOnState=true; bzT0=millis(); bzLeft--; } else { bzLeft=0; } } }

// ---------- Protocolo UART ----------
struct __attribute__((packed)) Payload {
  uint8_t ch[8];     // ch[7] = delta em detentes (int8 + 128)
  uint8_t sw;        // 0=S1, 1=S2, 2=CAL
  uint8_t seqShort;  // contador de cliques curtos
  uint8_t seqLong;   // contador de cliques longos
};

static uint8_t crc8(const uint8_t* d, size_t n){ uint8_t c=0x00; for(size_t i=0;i<n;i++){ uint8_t x=d[i]; for(uint8_t b=0;b<8;b++){ uint8_t mix=(c^x)&1; c>>=1; if(mix) c^=0x8C; x>>=1; } } return c; }

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

// ---------- UI ----------
enum Layer : uint8_t { LAYER_EXT=0, LAYER_INT=1 };
enum UiMode : uint8_t { UI_HOME=0, UI_MENU=1 };
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
void loadCfg(){ EEPROM.begin(512); EEPROM.get(0, persist); if (persist.magic!=EEPROM_MAGIC){ cfg={0,0,1,1,2, 76,1,2,1,1,10}; persist.magic=EEPROM_MAGIC; persist.cfg=cfg; EEPROM.put(0,persist); EEPROM.commit(); } else cfg=persist.cfg; }
void saveCfg(){ persist.magic=EEPROM_MAGIC; persist.cfg=cfg; EEPROM.put(0,persist); EEPROM.commit(); }

const char* CTRL_MODES[] = { "Veiculo 1","Veiculo 2","Veiculo 3" };
const char* PILOT_MODES[] = { "Responsivo","Alcance","Iniciante","Personal" };
const char* RATES[] = { "250K","1M","2M" };
const char* PAs[]   = { "MIN","LOW","HIGH","MAX" };
const char* CRCs[]  = { "8b","16b" };

Payload cur{}, prev{};
uint8_t prevSeqShort=0, prevSeqLong=0;
uint32_t tLastRx=0; bool linkOk=false;
uint32_t lastLongHandledMs=0; const uint16_t LONG_COOLDOWN_MS=500; // extra proteção

// ---- Navegação/menus ----
void menuReset(){ currentMenu=M_MAIN; cursor=0; }
void menuScroll(int8_t dir){ int8_t maxIdx= (currentMenu==M_MAIN?5:(currentMenu==M_OPTIONS?5:6)); cursor+=dir; if(cursor<0) cursor=maxIdx; if(cursor>maxIdx) cursor=0; }
void menuChangeValue(int8_t step){
  if (currentMenu==M_MAIN){
    if      (cursor==1){ cfg.ctrlMode  = (cfg.ctrlMode + step + 3)%3; saveCfg(); }
    else if (cursor==2){ cfg.pilotMode = (cfg.pilotMode+ step + 4)%4; saveCfg(); }
    else if (cursor==3){ cfg.theme     = (cfg.theme    + step + 2)%2; saveCfg(); }
  } else if (currentMenu==M_OPTIONS){
    if      (cursor==1){ cfg.telemOn ^=1; saveCfg(); }
    else if (cursor==2){ int v=cfg.smooth + step; if(v<0)v=5; if(v>5)v=0; cfg.smooth=v; saveCfg(); }
    else if (cursor==5){ Cfg keep=cfg; keep.ctrlMode=0; keep.pilotMode=0; keep.telemOn=1; keep.smooth=2; keep.rf_ch=76; keep.rf_rate=1; keep.rf_pa=2; keep.rf_crc=1; keep.rf_aa=1; keep.rf_retries=10; keep.theme=cfg.theme; cfg=keep; saveCfg(); }
  } else if (currentMenu==M_RADIO){
    if      (cursor==1){ int ch=cfg.rf_ch+step; if(ch<0)ch=125; if(ch>125)ch=0; cfg.rf_ch=ch; saveCfg(); }
    else if (cursor==2){ cfg.rf_rate=(cfg.rf_rate+step+3)%3; saveCfg(); }
    else if (cursor==3){ cfg.rf_pa  =(cfg.rf_pa  +step+4)%4; saveCfg(); }
    else if (cursor==4){ cfg.rf_crc^=1; saveCfg(); }
    else if (cursor==5){ cfg.rf_aa ^=1; saveCfg(); }
    else if (cursor==6){ int rr=cfg.rf_retries+step; if(rr<0)rr=15; if(rr>15)rr=0; cfg.rf_retries=rr; saveCfg(); }
  }
}
void menuEnter(){
  if (currentMenu==M_MAIN){
    if      (cursor==0) uiMode=UI_HOME;
    else if (cursor==4) { currentMenu=M_OPTIONS; cursor=0; }
    else if (cursor==5) { currentMenu=M_RADIO;   cursor=0; }
  } else if (cursor==0){
    currentMenu=M_MAIN; cursor=0;
  }
}

void onShortClick(){ if (uiMode==UI_HOME){ focusLayer=(focusLayer==LAYER_EXT)?LAYER_INT:LAYER_EXT; } else { menuEnter(); } }
void onLongPress(){ if (uiMode==UI_HOME){ uiMode=UI_MENU; menuReset(); } else { uiMode=UI_HOME; } }

// ---- Render HUD ----
void drawFocusBadge(U8G2& u8,bool f){ if(f) u8.drawBox(122,0,6,6); else u8.drawFrame(122,0,6,6); }
void drawCard(U8G2& u8,const char* title,const char* v1=nullptr,const char* v2=nullptr){ u8.clearBuffer(); u8.setFont(u8g2_font_6x13B_tf); u8.drawStr(2,12,title); u8.drawHLine(0,14,128); u8.setFont(u8g2_font_6x10_tf); if(v1)u8.drawStr(2,32,v1); if(v2)u8.drawStr(2,48,v2); u8.sendBuffer(); }

void renderHome(){
  const char* intTitle = INT_CARDS[intCard];
  const char* extTitle = EXT_CARDS[extCard];

  // EXTERNA
  u8g2_ext.clearBuffer(); drawFocusBadge(u8g2_ext, focusLayer==LAYER_EXT);
  if      (strcmp(extTitle,"Altimetria")==0)  drawCard(u8g2_ext,"Altimetria","Aguardando...","---");
  else if (strcmp(extTitle,"Bateria RX")==0)  drawCard(u8g2_ext,"Bateria RX","V: N/A","%: N/A");
  else if (strcmp(extTitle,"RSSI/LQ")==0)     drawCard(u8g2_ext,"RSSI/LQ","RSSI: N/A","LQ: N/A");
  else if (strcmp(extTitle,"Velocidade")==0)  drawCard(u8g2_ext,"Velocidade","N/A",nullptr);
  else if (strcmp(extTitle,"Distancia")==0)   drawCard(u8g2_ext,"Distancia","N/A",nullptr);
  u8g2_ext.sendBuffer();

  // INTERNA
  u8g2_int.clearBuffer(); drawFocusBadge(u8g2_int, focusLayer==LAYER_INT);
  if      (strcmp(intTitle,"Bat TX:Nano")==0) drawCard(u8g2_int,"Bat TX:Nano","V: N/A","%: N/A");
  else if (strcmp(intTitle,"Bat TX:ESP")==0)  drawCard(u8g2_int,"Bat TX:ESP","V: N/A","%: N/A");
  else if (strcmp(intTitle,"Status Link")==0) drawCard(u8g2_int,"Status Link", linkOk? "OK":"Sem sinal", nullptr);
  else if (strcmp(intTitle,"Modo/Piloto")==0){ char l1[24]; snprintf(l1,sizeof(l1),"Ctrl: %u",(unsigned)(cfg.ctrlMode+1)); const char* pm=PILOT_MODES[cfg.pilotMode%4]; char l2[24]; snprintf(l2,sizeof(l2),"Pilot: %s",pm); drawCard(u8g2_int,"Modo/Piloto",l1,l2); }
  else if (strcmp(intTitle,"Diag")==0){ char l1[24]; snprintf(l1,sizeof(l1),"CH:%u RATE:%s",cfg.rf_ch,RATES[cfg.rf_rate]); char l2[24]; snprintf(l2,sizeof(l2),"PA:%s CRC:%s",PAs[cfg.rf_pa],CRCs[cfg.rf_crc]); drawCard(u8g2_int,"Diag",l1,l2); }
  u8g2_int.sendBuffer();
}

// ---- Render MENU **full-screen** ----
void drawMenuList(U8G2& u8,const char* title,const char* const* items,uint8_t count){
  u8.clearBuffer();
  u8.setFont(u8g2_font_6x13B_tf); u8.drawStr(2,12,title); u8.drawHLine(0,14,128); u8.setFont(u8g2_font_6x10_tf);
  const uint8_t rowH=12;
  for(uint8_t i=0;i<count;i++){ int y=28 + i*rowH; if(i==cursor) u8.drawStr(2,y,">"); u8.drawStr(12,y,items[i]); }
  u8.sendBuffer();
}

void renderMenu(){
  // EXTERNA: cabeçalho claro de que estamos no menu
  u8g2_ext.clearBuffer();
  u8g2_ext.setFont(u8g2_font_7x14B_tf);
  u8g2_ext.drawStr(10, 20, "MENU ATIVO");
  u8g2_ext.setFont(u8g2_font_6x10_tf);
  u8g2_ext.drawStr(10, 38, "Gire p/ navegar");
  u8g2_ext.drawStr(10, 52, "Clique curto = Enter");
  u8g2_ext.sendBuffer();

  // INTERNA: a lista do menu
  switch(currentMenu){
    case M_MAIN: {
      const char* items[]={"<- Voltar","Modos do controle","Modo de pilotagem","Tema (Claro/Escuro)","Opcoes","Radio (NRF24)"};
      drawMenuList(u8g2_int,"MENU",items,6);
    } break;
    case M_OPTIONS: {
      const char* items[]={"<- Voltar","Telemetria (On/Off)","Smooth (0..5)","Calibracao (stub)","Salvar cfg (auto)","Resetar cfg"};
      drawMenuList(u8g2_int,"OPCOES",items,6);
    } break;
    case M_RADIO: {
      char chLine[20];  snprintf(chLine,sizeof(chLine),"Canal RF: %u",cfg.rf_ch);
      char rateLine[20];snprintf(rateLine,sizeof(rateLine),"DataRate: %s",RATES[cfg.rf_rate]);
      char paLine[20];  snprintf(paLine,sizeof(paLine),"PA: %s",PAs[cfg.rf_pa]);
      char crcLine[20]; snprintf(crcLine,sizeof(crcLine),"CRC: %s",CRCs[cfg.rf_crc]);
      char aaLine[20];  snprintf(aaLine,sizeof(aaLine),"AutoAck: %s",cfg.rf_aa?"On":"Off");
      char rtLine[20];  snprintf(rtLine,sizeof(rtLine),"Retries: %u",cfg.rf_retries);
      const char* items[]={ "<- Voltar", chLine, rateLine, paLine, crcLine, aaLine, rtLine };
      drawMenuList(u8g2_int,"RADIO",items,7);
    } break;
  }
}

// ---- Aplicar delta do encoder ----
void applyDeltaSteps(int16_t steps){
  if (steps==0) return;
  int8_t dir = (steps>0)? +1 : -1;
  for (int16_t k=0;k<abs(steps);k++){
    if (uiMode==UI_HOME){
      if (focusLayer==LAYER_EXT) extCard=(extCard+(dir>0?1:-1)+EXT_CARD_COUNT)%EXT_CARD_COUNT;
      else                       intCard=(intCard+(dir>0?1:-1)+INT_CARD_COUNT)%INT_CARD_COUNT;
    } else {
      menuScroll(dir>0?1:-1);
      menuChangeValue(dir>0?+1:-1);
    }
  }
}

void setup(){
  pinMode(BUZZER_PIN, OUTPUT); digitalWrite(BUZZER_PIN, LOW);
  Serial.begin(38400);

  u8g2_int.begin(); u8g2_ext.begin();
  loadCfg();

  // Splash
  u8g2_ext.clearBuffer(); u8g2_ext.setFont(u8g2_font_6x13B_tf); u8g2_ext.drawStr(6,28,"OpenRC AeroLink"); u8g2_ext.drawStr(6,44,"HUD ESP8266"); u8g2_ext.sendBuffer();
  u8g2_int.clearBuffer(); u8g2_int.setFont(u8g2_font_6x10_tf); u8g2_int.drawStr(8,32,"Aguardando link..."); u8g2_int.sendBuffer();
}

void loop(){
  buzzerTask();

  // RX
  Payload rx; bool got = readFrame(Serial, rx);
  if (got){ cur=rx; tLastRx=millis(); }
  linkOk = (millis()-tLastRx) < 300;

  // Eventos por contadores
  uint8_t dShort = cur.seqShort - prevSeqShort;
  uint8_t dLong  = cur.seqLong  - prevSeqLong;

  if (dShort){ for(uint8_t i=0;i<dShort;i++){ onShortClick(); buzz(BZ_CLICK);} prevSeqShort = cur.seqShort; }
  if (dLong){
    for(uint8_t i=0;i<dLong;i++){
      uint32_t now=millis();
      if (now - lastLongHandledMs >= LONG_COOLDOWN_MS){
        onLongPress(); buzz(BZ_LONG); lastLongHandledMs = now;
      }
    }
    prevSeqLong = cur.seqLong;
  }

  // Giro (detentes)
  int8_t det = (int8_t)cur.ch[7] - 128;
  if (det) applyDeltaSteps(det);

  // Render ~30 FPS
  static uint32_t tDraw=0;
  if (millis()-tDraw >= 33){
    tDraw=millis();
    if (uiMode==UI_HOME) renderHome(); else renderMenu();
  }
}
