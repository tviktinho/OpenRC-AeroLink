# OpenRC AeroLink

**OpenRC AeroLink** é um sistema open-source de controle remoto sem fio modular, desenvolvido para funcionar tanto em **simuladores de PC** quanto em **aeronaves reais**.  
Baseado em **Arduino (Nano/Mega)**, **ESP8266/ESP32** e **nRF24L01 PA/LNA**, o projeto combina **transmissão robusta**, **HUD visual** em OLEDs e **arquitetura expansível**.

---

## 📸 Visão Geral
![Controle Frontal](docs/images/controle-frontal.jpg)
![Controle Interno](docs/images/controle-interno.jpg)

---

## 📖 História do Projeto

A história do **OpenRC AeroLink** começou com um controle antigo de simulador de PC que, devido à montagem precária, acabou guardado por anos.  
Na faculdade, durante a disciplina de Sistemas Digitais, surgiu a ideia de reconstruí-lo com eletrônica confiável e expandir suas funcionalidades.

O projeto passou por **três grandes etapas**:

1. **Etapa 1 — MEGA + Cabo USB**  
   O Arduino MEGA foi ligado diretamente aos potenciômetros e switches, atuando como joystick via cabo USB.  
   - Funcionava muito bem em simuladores, mas era limitado ao uso no PC.  
   ![Etapa 1](docs/images/etapa1-mega-usb.jpg)

2. **Etapa 2 — NANO + Rádio nRF24L01 (PCB artesanal)**  
   Foi criada uma primeira versão sem fio usando Arduinos Nano e módulos nRF24L01.  
   A transmissão de dados funcionava, mas a montagem artesanal com protoboard e PCB improvisada trouxe muitos problemas de confiabilidade.  
   ![Etapa 2](docs/images/etapa2-nano-rf.jpg)

3. **Etapa 3 — PCB dedicada + ESP + HUD**  
   Após projetar e mandar fabricar uma PCB própria, o sistema ganhou robustez.  
   O transmissor (NANO_TX) foi integrado com HUD em duas telas OLED controladas por um ESP8266, e o receptor evoluiu para ESP32, garantindo maior alcance e recursos como PWM/PPM estáveis.  
   ![Etapa 3](docs/images/etapa3-pcb-esp.jpg)

Hoje, o **OpenRC AeroLink** está consolidado como uma plataforma modular que pode ser usada tanto em simuladores quanto em aeromodelos reais.

---

## ✨ Recursos
- 📡 Transmissão sem fio estável (nRF24L01 PA/LNA).  
- 🎮 Compatível com **vJoy/Simuladores** via Python.  
- 🛩️ Suporte a **aeronaves reais** (PWM/PPM para servos/ESC).  
- 🖥️ **HUD** com 2 telas OLED (informações de canais, modo, link e calibração).  
- 🔊 Feedback sonoro (buzzer para modos e falhas).  
- ⚙️ Arquitetura modular e bem documentada.

---

## 🚀 Como usar
1. **Transmissor (NANO_TX)**: leitura dos eixos + switches, envio via rádio, UART para HUD.  
2. **HUD (ESP8266_TX)**: exibe canais, switches, status de link e calibração.  
3. **Receptor (ESP32_RX ou MEGA_RX)**: gera sinais PWM/PPM para servos/ESC ou atua como ponte com PC.  
4. **Simulador (MEGA_SIM + Python)**: conecta ao PC e usa `mega_joystick.py` para mapear sinais no vJoy.

---

## 📌 Licença
Este projeto é distribuído sob licença **MIT**. Consulte o arquivo [LICENSE](LICENSE).
