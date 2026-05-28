# OpenRC AeroLink — Roadmap

## ✅ Concluído
- Etapa 1: MEGA + cabo USB → simulador.
- Etapa 2: NANO + rádio nRF24L01 (PCB artesanal).
- Etapa 3: PCB dedicada, nRF24 PA/LNA, HUD ESP8266, RX ESP32.

## 🚧 Etapa 4 — Upgrade ELRS (em andamento)

> Objetivo: substituir o link 2.4 GHz nRF24 por **ExpressLRS 900 MHz** (longo alcance, robusto, padrão da indústria) mantendo compatibilidade com os receptores antigos durante a transição.

### Fase 0 — Preparação ✅
- [x] Restaurar `NANO_TX.ino` v1 original do commit `77a5226` em `firmware/legacy/`
- [x] Criar estrutura de pastas `firmware/TX_NANO_v2/` e `firmware/Heltec_V2_TX_ELRS/`

### Fase 1 — ELRS oficial para validação 🚧
- [x] `firmware/TX_NANO_v2/`: NANO_TX com **nRF24 + CRSF paralelo** (50 Hz)
  - [x] Header reutilizável `crsf_tx.h` (CRSF v3, RC_CHANNELS_PACKED 0x16)
  - [x] Mapeamento: 7 pots → CH1..CH7, 4 switches → CH8 bitmask, CH9..CH16 reservado
  - [x] Compatibilidade nRF24 100% preservada (struct, addr, canal, baud iguais)
- [x] `firmware/Heltec_V2_TX_ELRS/`: layout custom + docs
  - [x] `heltec_v2_sx1276.json` — layout custom para ExpressLRS Configurator
  - [x] `README.md` — passo-a-passo do flash
  - [x] `pinout_diagram.md` — mapa completo de pinos
  - [x] `troubleshooting.md` — problemas comuns
- [ ] **Aguardando hardware**: comprar RX ELRS 900 MHz (EPW6 / SuperD 900 / R24-D)
- [ ] **Aguardando teste**: flash do Heltec + bind do RX + validação end-to-end

### Fase 2 — Firmware customizado (TCC) 🔭
- [ ] `firmware/Heltec_V2_TX_custom/`: firmware Arduino próprio no Heltec
  - [ ] Implementar lado TX do protocolo ELRS sobre LoRa (sync word, FHSS, frames)
  - [ ] UI customizada em 2 OLEDs (interna do Heltec + 1 externa via I²C)
  - [ ] Migrar encoder, botão, buzzer do ESP8266 para o Heltec
  - [ ] Portar lógica de cards/menus de `firmware/TX_ESP8266/` (referência mantida)
  - [ ] **Aposentar ESP8266** fisicamente da PCB
- [ ] Telemetria bidirecional (RSSI, LQ, bateria do RX) via CRSF de volta ao Nano

### Fase 3 — Limpeza pós-upgrade (futuro)
- [ ] Corrigir `firmware/TX_NANO/NANO_TX/NANO_TX.ino` (atualmente com código do FC ESP32 por engano)
- [ ] Preencher `firmware/MAVlink/ESP32.ino` (atualmente vazio) ou remover
- [ ] Preencher `docs/PCB-RELATORIO.md` (atualmente vazio)
- [ ] Resolver inconsistência de endereço nRF24 em `firmware/WALL-E/` (TX `"0001"` ≠ RX `"01010"`)
- [ ] Consolidar os 3 firmwares ESP32 FC paralelos em um único (`ESP32/`, `ESP32_RX/`, `MAVlink/`)

## 📌 Roadmap original (mantido para referência)
- Implementar **telemetria bidirecional** (bateria RX, RSSI, etc.) — coberto pela Fase 2
- Adicionar **OTA** (atualização remota ESP) — ELRS já tem OTA via WiFi
- Desenvolver **mecanismo de failsafe avançado** (mixers, fail-throttle)
- Suporte a **flight modes** e **curvas de resposta**
- Perfis de aeronave múltiplos (config salvável)

## 🌐 Futuro
- Integração com **planejamento de voo** (waypoints)
- Painel web para configuração (parcialmente coberto pelo web UI do ELRS)
- Testes em campo com aeronaves reais
- FC própria com receptor ELRS integrado (Fase 2+)
