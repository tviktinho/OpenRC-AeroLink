# OpenRC AeroLink — Relatório do Projeto

## 1. Introdução
O OpenRC AeroLink nasceu da reconstrução de um controle antigo de simulador. O projeto passou por **três grandes etapas**:  
1. **Etapa 1:** Arduino MEGA via cabo USB, atuando como joystick.  
2. **Etapa 2:** Arduino NANO + nRF24L01 em PCB artesanal (primeiro TX/RX sem fio).  
3. **Etapa 3:** PCB dedicada, rádio PA/LNA, integração com ESP8266 (HUD) e ESP32 (RX moderno).

---

## 2. Evolução
- **Laboratório de Robótica da UFU** foi fundamental: forneceu materiais, espaço para soldagem e impressões 3D.  
- A cada etapa, houve pausas estratégicas (principalmente nas transições de hardware).  
- O projeto atual já suporta **HUD em tempo real**, **alcance ampliado com PA/LNA**, **suporte a simuladores** e **PWM para servos/ESC**.

---

## 3. Arquitetura Atual
[ TX — NANO_TX ] --SPI--> [ nRF24L01 (PA/LNA) ] )))))) (((((( [ nRF24L01 ] <--SPI-- [ RX — ESP32_RX ou MEGA_RX ]
| |
+--UART--> [ ESP8266_TX (HUD: OLED + buzzer) ] +--> [ PWM/PPM para servos/ESC ] 
+--> [ PC ] <—USB— [ MEGA_SIM ] <—Serial— [ RX ]

---

## 4. Códigos e Funções
- **NANO_TX:** lê potenciômetros e switches, envia pacote rádio, UART para HUD.  
- **NANO_RX:** RX legado, gera PWM/PPM.  
- **ESP8266_TX:** HUD, 2 telas OLED, buzzer, feedback de calibração e link.  
- **ESP32_RX:** RX moderno, PWM/PPM, pronto para telemetria futura.  
- **MEGA_RX:** RX compatível com PWM e ponte para simulador.  
- **MEGA_SIM:** sketch + Python (`mega_joystick.py`) para PC reconhecer como joystick via vJoy.

---

## 5. Roadmap
- Telemetria bidirecional (bateria RX, RSSI, etc.).  
- Atualizações OTA (ESP32/ESP8266).  
- Flight modes e mixers.  
- Planejamento de voo (waypoints).  
- Perfis de aeronaves múltiplos.

---

## 6. Créditos
- **Laboratório de Robótica/UFU** pelo suporte.  
- Leonardo e Bruno pelo incentivo.
- Comunidade Arduino/ESP por bibliotecas.  
