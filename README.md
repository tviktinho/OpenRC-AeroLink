# OpenRC AeroLink

**OpenRC AeroLink** é um sistema open-source de controle remoto sem fio modular, desenvolvido para funcionar tanto em **simuladores de PC** quanto em **aeronaves reais**.  
Baseado em **Arduino (Nano/Mega)**, **ESP8266/ESP32** e **nRF24L01 PA/LNA**, o projeto combina **transmissão robusta**, **HUD visual** em OLEDs e **arquitetura expansível**.

---

## ✨ Recursos
- 📡 Transmissão sem fio estável (nRF24L01 PA/LNA).  
- 🎮 Compatível com **vJoy/Simuladores** via Python.  
- 🛩️ Suporte a **aeronaves reais** (PWM/PPM para servos/ESC).  
- 🖥️ **HUD** com 2 telas OLED (informações de canais, modo, link e calibração).  
- 🔊 Feedback sonoro (buzzer para modos e falhas).  
- ⚙️ Arquitetura modular e bem documentada.

---

## 📂 Estrutura do Repositório
firmware/
├─ NANO_TX/ # Transmissor principal
├─ NANO_RX/ # RX legado (manutenção)
├─ ESP8266_TX/ # HUD (OLEDs + buzzer)
├─ ESP32_RX/ # RX moderno (PWM/PPM)
├─ MEGA_RX/ # RX compatível + ponte simulador
└─ MEGA_SIM/ # Sketch para simulador PC

software/
└─ pc/
└─ mega_joystick.py # Script Python + vJoy

hardware/
└─ pcb-nano/ # Layouts da PCB (docs separados)

docs/
├─ RELATORIO.md
├─ PCB-RELATORIO.md
├─ PROTOCOLO.md
└─ ROADMAP.md


---

## 🚀 Como usar
1. **Transmissor (NANO_TX)**: leitura dos eixos + switches, envio via rádio, UART para HUD.  
2. **HUD (ESP8266_TX)**: exibe canais, switches, status de link e calibração.  
3. **Receptor (ESP32_RX ou MEGA_RX)**: gera sinais PWM/PPM para servos/ESC ou atua como ponte com PC.  
4. **Simulador (MEGA_SIM + Python)**: conecta ao PC e usa `mega_joystick.py` para mapear sinais no vJoy.

---

## 📌 Licença
Este projeto é distribuído sob licença **MIT**. Consulte o arquivo [LICENSE](LICENSE).
