// ESP8266 + 2x SSD1306 (U8g2, ambos em I2C por software)
// Onboard (integrado): SCL=D5 (GPIO14), SDA=D6 (GPIO12)
// Externo:             SCL=D1 (GPIO5),  SDA=D2 (GPIO4)  [com pull-ups 4.7k em D1 e D2 para 3V3]

#include <Arduino.h>
#include <U8g2lib.h>

// IMPORTANTE: Ordem dos pinos na U8g2 SW I2C é: clock, data, reset

// OLED integrado (confirmado pelo seu scan: SCL=D5, SDA=D6)
U8G2_SSD1306_128X64_NONAME_F_SW_I2C oledInt(
  U8G2_R0,
  /* clock=*/ D5, /* data=*/ D6, /* reset=*/ U8X8_PIN_NONE
);

// OLED externo (nos pinos D1/D2 com pull-ups)
U8G2_SSD1306_128X64_NONAME_F_SW_I2C oledExt(
  U8G2_R0,
  /* clock=*/ D1, /* data=*/ D2, /* reset=*/ U8X8_PIN_NONE
);

void drawDemo(U8G2 &d, const char* title, uint32_t t) {
  d.clearBuffer();
  d.setFont(u8g2_font_6x10_tf);
  d.setCursor(0,10);
  d.print(title);

  // Barrinhas animadas (8 “canais” só para teste visual)
  for (int i = 0; i < 8; i++) {
    int col = (i < 4) ? 0 : 1;
    int row = (i % 4);
    int x = 2 + col * 64;
    int y = 14 + row * 13;
    int w = 60, h = 8;
    d.drawFrame(x, y, w, h);
    int val = ((t / 100) + (i * 20)) % 255;
    int bar = map(val, 0, 255, 0, w - 2);
    d.drawBox(x + 1, y + 1, bar, h - 2);
  }

  d.setCursor(0, 63);
  d.print("t="); d.print(t);
  d.sendBuffer();
}

void setup() {
  // U8g2 SW I2C não usa Wire/SoftwareWire; inicializamos direto:
  oledInt.begin();
  oledExt.begin();

  // Mensagens iniciais
  oledInt.clearBuffer();
  oledInt.setFont(u8g2_font_6x10_tf);
  oledInt.drawStr(0,10, "Integrado OK (D5/D6)");
  oledInt.sendBuffer();

  oledExt.clearBuffer();
  oledExt.setFont(u8g2_font_6x10_tf);
  oledExt.drawStr(0,10, "Externo OK (D1/D2)");
  oledExt.sendBuffer();

  delay(800);
}

void loop() {
  uint32_t t = millis();
  drawDemo(oledInt, "OLED Integrado", t);
  drawDemo(oledExt, "OLED Externo",   t);
  delay(50);
}
                                                                                                                                                                                                                                                                                                                                                                                        