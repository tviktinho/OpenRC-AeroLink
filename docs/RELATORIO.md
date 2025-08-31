1) Visão Geral

O projeto é a evolução de um controle originalmente pensado apenas para simuladores no PC. O projeto reestruturou a eletrônica, adicionou telemetria visual via OLED, comunicação sem fio nRF24L01 com PA/LNA, suporte a simuladores (vJoy) e preparo para voos reais com receptor dedicado. O objetivo é entregar um TX/RX universal: o mesmo transmissor serve para simulação em PC e controle de aeromodelos, com HUD informativo e arquitetura modular.

2) Linha do Tempo (3 Etapas)
Etapa 1 — MEGA + Cabo (Simulador)

Hardware: Arduino MEGA preso na traseira do controle.

Entrada: potenciômetros ligados diretamente ao MEGA.

Operação: via USB com script no PC para fazer boot como joystick (vJoy).

Resultado: excelente para simulador, mas limitado ao uso com PC.

Etapa 2 — NANO + Rádio (nRF24L01) em protoboard/PCB artesanal

Meta: adicionar TX/RX sem fio com dois Arduinos NANO e nRF24L01 (sem antena externa).

Status: comunicação TX→RX funcionou, mas a integração total falhou (curtos, dificuldades de montagem).

Conseqüência: pausa estratégica para repensar layout e robustez.

Etapa 3 — Placa dedicada + Integração com ESP + PA/LNA

Hardware: PCB própria (compatível com NANO), montagem robusta, solda revisada.

Upgrades: módulo nRF24L01 PA/LNA (maior alcance) e ESP dedicado ao HUD (aliviando o NANO).

Estados atuais:

Transmissão estável com PA/LNA.

HUD via ESP8266 com duas telas OLED 0,96" e buzzer.

Caminho PC (simulador) por MEGA_SIM + Python/vJoy.

Caminho Aeromodelo por RX dedicado (ESP32_RX/MEGA_RX) com saídas PWM/PPM.