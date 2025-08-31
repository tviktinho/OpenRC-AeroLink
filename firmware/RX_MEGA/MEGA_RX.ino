// === MEGA 2560 — Recebe 8+2 canais via Serial1 e envia CSV para o PC ===
// Protocolo: 0xAA 0x55 [p0..p7] [s1] [s2] [chkXOR de p0..s2]

struct Frame {
  uint8_t p[8];
  uint8_t s1;
  uint8_t s2;
} f;

enum { ST_AA, ST_55, ST_PAY, ST_CHK } st = ST_AA;
uint8_t buf[10];  // p0..p7(8) + s1 + s2 = 10
uint8_t idx = 0;

bool readFrame() {
  while (Serial1.available()) {
    uint8_t b = Serial1.read();
    switch (st) {
      case ST_AA: if (b == 0xAA) st = ST_55; break;
      case ST_55: st = (b == 0x55) ? ST_PAY : ST_AA; break;
      case ST_PAY:
        buf[idx++] = b;
        if (idx == 10) st = ST_CHK;
        break;
      case ST_CHK: {
        uint8_t chk = 0; for (uint8_t i = 0; i < 10; i++) chk ^= buf[i];
        bool ok = (chk == b);
        st = ST_AA;
        if (ok) {
          for (uint8_t i = 0; i < 8; i++) f.p[i] = buf[i];
          f.s1 = buf[8]; f.s2 = buf[9];
          idx = 0;
          return true;
        } else {
          idx = 0; // descarta e ressincroniza
        }
      } break;
    }
  }
  return false;
}

void setup() {
  Serial.begin(115200);   // USB -> PC (CSV)
  Serial1.begin(115200);  // Link com o Nano RX
  Serial.println(F("# MEGA pronto: aguardando frames 8+2 via Serial1..."));
}

void loop() {
  if (readFrame()) {
    // CSV p0..p7,s1,s2  (p em 0..255; switches 0/1 -> 0/255)
    Serial.print(f.p[0]);
    for (int i = 1; i < 8; i++) { Serial.print(','); Serial.print(f.p[i]); }
    Serial.print(','); Serial.print(f.s1 ? 255 : 0);
    Serial.print(','); Serial.print(f.s2 ? 255 : 0);
    Serial.println();
  }
}
