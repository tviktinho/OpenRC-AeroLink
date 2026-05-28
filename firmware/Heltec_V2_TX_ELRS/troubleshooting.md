# Troubleshooting — ELRS no Heltec V2 (Fase 1)

Documentação do debugging real da Fase 1 do upgrade ELRS. Registra **tudo que tentamos**, o que **NÃO funcionou**, e o **diagnóstico final** — pra você ou outra pessoa que enfrentar o mesmo problema não percorrer o mesmo caminho.

> 🎓 **Pra usar como referência no TCC**: esse documento é exemplo prático de processo de debug sistemático em sistema embarcado RF. Várias hipóteses descartadas, diagnóstico por exclusão, e identificação de falha em hardware mascarada por sintomas de software.

---

## 📋 Resumo executivo

**Sintoma**: Bind ELRS RF não fechava entre TX (Heltec V2 + ELRS 4.0.1) e RX (ELRS-915M genérico chinês) mesmo com toda configuração lógica correta.

**Diagnóstico final**: **RX 915M defeituoso** (ou clone com hardware fora da spec). O TX estava transmitindo corretamente; o RX não conseguia receber **nem a 5 cm de distância**, o que é fisicamente impossível com hardware funcional (SX1276 tem sensibilidade -139 dBm).

**Solução recomendada**: Comprar RX confiável (Happymodel EP1RX 900, BetaFPV ELRS Lite Nano RX 900) — evitar clones genéricos chineses sem marca.

**Tempo investido**: ~8 horas de debug.

---

## 🔬 Cronologia do debugging

### Fase 1A — Setup inicial (tudo "deveria" funcionar)

| Item | Estado |
|---|---|
| Heltec V2 com ELRS 4.0.1 (firmware DIY ESP32 RFM95 900MHz TX) | ✅ flashado |
| Hardware.json customizado (pinout Heltec V2) | ✅ aplicado via `/hardware.html` |
| Binding phrase `aerolink-tcc-victor-2026` | ✅ configurada em TX e RX |
| Domain regulatório FCC915 (Brasil) | ✅ ambos |
| RX 915M com ELRS 4.0.1 + UID `190,168,40,211,143,5` | ✅ flashado |
| UART baud 400000 baud (Nano AVR não gera 420000 limpo) | ✅ ambos |
| Cabeamento Nano↔Heltec com divisor 3k3/1k8 | ✅ feito |
| Cabeamento RX↔ESP32 (T→GPIO32, R→GPIO33, V→5V, G→GND) | ✅ correto |

**Resultado**: LED do RX piscando lento (não bindado), sniffer ESP32 mostrando `[NO LINK] ok=0`.

### Fase 1B — Hipóteses testadas e descartadas

#### Hipótese 1: Cabeamento RX↔ESP32 invertido
- **Teste**: trocou T↔R nos pinos do ESP32
- **Resultado**: nada mudou
- **Conclusão**: descartada

#### Hipótese 2: Baud rate UART do RX errado (420000 ≠ 400000)
- **Teste**: mudou UART baud do RX pra 400000 no painel web
- **Resultado**: configuração persistiu, mas bind continuou sem fechar
- **Conclusão**: configuração correta, mas não era a causa

#### Hipótese 3: Firmware 4.0.1 do RX corrompido
- **Teste**: re-flash do RX com versão 4.0.1 + ☑ "Erase before flash"
- **Resultado**: firmware persistido OK, mas bind continuou sem fechar
- **Conclusão**: descartada

#### Hipótese 4: Config residual da v3.3.0 inicial no Heltec
- **Teste**: re-flash do Heltec com versão 4.0.1 + ☑ "Erase before flash" + re-arrastar hardware.json
- **Resultado**: bind continuou sem fechar
- **Conclusão**: descartada (mas Erase é boa prática sempre)

#### Hipótese 5: Power Level control errado
- **Teste**: mudou de `via SEMTECH` pra `via ESP DACWRITE` (foi um erro do guia)
- **Resultado**: ficou pior, voltou pra `via SEMTECH`
- **Conclusão**: `via SEMTECH` é o correto pra Heltec V2 (sem PA externo)

#### Hipótese 6: Default Power baixo demais
- **Teste**: aumentou Default Power no Hardware Layout de 25mW → 100mW + SAVE TARGET CONFIGURATION + RST
- **Resultado**: UI persistiu mudança, mas `max-power` no JSON exportado continuou em 2
- **Conclusão**: **descoberta de um problema real** — `max-power` está hardcoded ou clampado no firmware

#### Hipótese 7: Bug do firmware DIY ESP32 RFM95 TX 4.0.1 que limita max-power
- **Teste 1**: editou models.json com `"max-power":5` explícito + importou via Import/Export + RST
- **Resultado**: voltou pra 2 após RST
- **Teste 2**: editou hardware.json com `power_default:5`, `power_max:5`, `power_values:[-9,-6,0,14,17,20]` + re-arrastou + RST
- **Resultado**: outros campos persistiram, mas `max-power` voltou pra 2
- **Conclusão**: confirmado bug/limitação do firmware 4.0.1 DIY ESP32 RFM95 TX que limita `max-power` em índice 2 (= 0 dBm = 1 mW na tabela). Não há config via UI/JSON que sobrescreva.

#### Hipótese 8: Versão 4.0.1 pré-release é instável → tentar 3.5.6 estável
- **Teste**: tentou flashar 3.5.6
- **Bloqueio**: Configurator v1.7.11 (mais recente) **não tem campo Custom Hardware Layout exposto** pra DIY 900 TX em versões 3.x. Necessário compilar via PlatformIO + repo source clonado.
- **Conclusão**: viável mas exige ~1-2h de setup; descartado por complexidade vs incerteza de resolver

#### Hipótese 9: Heltec não está transmitindo de verdade
- **Teste**: monitorou se WiFi do Heltec cai quando Nano conecta (ELRS desliga WiFi durante TX ativa)
- **Resultado**: WiFi CAI ✅ → Heltec ESTÁ transmitindo
- **Conclusão**: TX confirmado funcional, descartada

### Fase 1C — Teste decisivo: proximidade extrema

**Procedimento**:
1. RX encostado fisicamente no Heltec (~2-5 cm)
2. Antenas paralelas e estendidas
3. Heltec + Nano + ESP32-C3 (sniffer) todos ligados
4. Aguardou 60 segundos observando LED do RX e sniffer

**Cálculo de viabilidade**:
- Heltec transmitindo em 1 mW (max-power: 2)
- Distância: ~5 cm
- Sensibilidade do RX SX1276: **-139 dBm**
- Path loss a 5 cm em 915 MHz: ~6 dB
- Sinal esperado no RX: ~0 dBm - 6 dB = **-6 dBm** (133 dB acima do threshold!)

**Resultado**: ainda `[NO LINK] ok=0` no sniffer, LED piscando lento.

**Conclusão**: **fisicamente impossível** com RX funcional. Hardware do RX está defeituoso ou totalmente fora da spec.

---

## 🎯 Diagnóstico final

**O RX ELRS-915M genérico chinês está defeituoso ou é clone falso.**

Possíveis causas físicas (não foi possível abrir o RX pra inspecionar):

1. **Chip RF diferente do esperado**: SX1278 (frequências sub-GHz diferentes) em vez de SX1276 — pode parecer compatível mas falha em decodificação CRC do ELRS oficial
2. **Cabo IPEX1 internamente rompido**: antena conectada na PCB mas sinal não chega ao chip
3. **Cristal oscilador fora de tolerância**: drift de frequência tira o RX da banda esperada
4. **Bootloader/firmware customizado do fabricante**: a embalagem dizia "ELRS 915MHz/2.4G 3.3.0" — o "/2.4G" é red flag (RX típico é uma banda só)
5. **PCB falsificada**: aparência de RX ELRS sem o hardware funcional

---

## 🚫 Lições aprendidas (importantes pro próximo)

### 1. Cuidado com RX/TX clones chineses sem marca

- ✅ **Compre de marca conhecida**: Happymodel, BetaFPV, Matek, RadioMaster, ELRS oficial
- ❌ **Evite**: "ExpressLRS 900MHz/2.4G generic", "ELRS Universal", produtos sem manual/datasheet, embalagem genérica
- A diferença de preço (R$50 vs R$80) não compensa as horas de debug

### 2. `max-power` no ELRS 4.0.1 DIY ESP32 RFM95 TX está bugado

- O firmware ignora `power_default` do `hardware.json` em runtime
- Edição manual do `models.json` com max-power alto não persiste
- Provável **limitação artificial** por segurança (sem PA externo + heatsink → não deixa subir potência)
- Workaround **conhecido mas não testado**: compilar firmware modificado via PlatformIO

### 3. Hardware.json no ELRS 4.x

- Roda em runtime via `/hardware.html`, **MAS** alguns campos podem ser hardcoded no firmware
- `power_values`, `serial_baudrate`, `radio_*` parecem ser respeitados
- `power_default`, `power_max`, `power_high` parecem ser **ignorados** (ou clampados)
- Testar caso a caso

### 4. ELRS Configurator v1.7.11

- Última versão (Out/2025). Não existem v1.8 ou v1.9.
- **Não tem Custom Hardware Layout exposto** pra targets DIY em 3.x e 4.x — limitação da UI
- Pra hardware totalmente custom: usar `/hardware.html` (4.x runtime) OU compilar via PlatformIO

### 5. Erase before flash é obrigatório em transições de versão

- 3.x → 4.x: SEM erase = config residual corrompe a nova versão
- 4.x → 4.y: ☑ pra evitar bugs por config legada
- Bind UID e binding phrase podem precisar ser re-aplicados depois

### 6. Diferença entre "Bound" (lógico) e "Linked" (RF)

- "Bound" no painel = UID gravada (TX e RX vão se reconhecer)
- "Linked" = comunicação RF ativa (pacotes trocados)
- **Bound sem Linked = TX e RX configurados mas não se ouvem fisicamente**

### 7. Tela base do Heltec não confirma transmissão ativa

- "ExpressLRS 4.0.1 200Hz 1:64 50mW TLM" aparece sempre que firmware está rodando
- Não distingue entre standby/WiFi mode e TX ativo
- **Confirmação real**: WiFi cai quando há link RF ativo

### 8. Riscos elétricos descobertos durante o debug

- **ESP32 NÃO é 5V-tolerant** (mito comum). Divisor de tensão 3k3/1k8 obrigatório entre Nano TX0 (5V) e GPIO 17 do Heltec
- **Ligar o Heltec sem antena durante TX pode danificar o SX1276** (alta reflexão de onda)
- **Power-on com sinal nos GPIOs antes do VCC** pode causar latch-up (Nano não pode mandar sinal pro Heltec antes do Heltec estar ligado)

---

## 🛠 Checklist de debug pra próxima vez (RX que não binda)

Faz **na ordem**, para na primeira que mostrar resultado positivo:

### Antes de tudo
- [ ] Antenas conectadas (TX e RX) — testar trocando elas entre si
- [ ] Cabeamento físico CRSF: T do RX → GPIO RX do FC, GND comum
- [ ] Alimentação 5V no RX (não 3.3V — alguns clones precisam de 5V)
- [ ] RX a ~30 cm de distância do TX (não muito perto, não muito longe)

### Software/config
- [ ] Mesma versão ELRS em TX e RX (major.minor)
- [ ] Mesma `binding phrase` (idêntica, case-sensitive)
- [ ] Mesmo `domain` regulatório (FCC915 = Brasil/EUA)
- [ ] UART baud do RX em 400000 (se TX é Nano AVR) ou 420000 (se TX é ESP32)
- [ ] Re-flash com **☑ Erase before flash** em TX e RX

### Confirma transmissão
- [ ] WiFi do TX cai quando handset (Nano) conecta? Se sim, está transmitindo
- [ ] Tela do TX mostra RSSI/LQ com valores reais (não traços)? Se sim, há link

### Teste de hardware
- [ ] **Proximidade extrema**: RX a 5 cm do TX. Se nem assim bindar, RX defeituoso
- [ ] Sniffer ESP32-C3 (este repo): se também mostrar `[NO LINK]`, problema é no RX/TX, não no FC
- [ ] **Substituir RX**: se tiver outro, testar. Se outro RX bindar, primeiro era defeito

### Compra recomendada se RX defeituoso
- Happymodel EP1RX 900 (R$60-80) — primeira escolha
- BetaFPV ELRS Lite Nano RX 900 (R$80-100)
- Matek R24-D 900 (R$120-150) — diversity, mais robusto

---

## 🟢 Problemas comuns no Configurator (mantido do troubleshooting antigo)

### "Cannot open COM port"
- Driver CP2102 não instalado → baixe em https://www.silabs.com/.
- Outro programa usando a porta (Arduino IDE, PlatformIO Monitor) → feche.
- No Linux, adicionar usuário ao grupo `dialout`:
  ```bash
  sudo usermod -aG dialout $USER
  ```
  (faz logout/login depois)

### Upload trava em 99%
- Velocidade de upload alta demais → baixar `upload_speed` no platformio.ini pra 460800

### "RADIO INIT FAIL" após boot
- Pinos SPI do `hardware.json` errados (re-confere SCK=5, MISO=19, MOSI=27, NSS=18)
- Cabos do `radio_*` invertidos ou soltos
- Hardware do SX1276 defeituoso

---

## 📚 Referências usadas

- ExpressLRS docs: https://www.expresslrs.org/
- DIY 900TX guide: https://www.expresslrs.org/quick-start/transmitters/diy900/
- Heltec V2 pinout: https://www.espboards.dev/esp32/heltec-wifi-lora-32-v2/
- SX1276 datasheet: Semtech AN1200 (sensibilidade, modulação LoRa)
- ELRS source code: https://github.com/ExpressLRS/ExpressLRS

---

## 🔄 Próximos passos quando comprar RX novo

1. Compra Happymodel EP1RX 900 (ou similar confiável)
2. Power-cycle ×3 → entra em WiFi `ExpressLRS RX`
3. Aba **Update** → arrasta firmware ELRS 4.0.1 BETAFPV ou Happymodel target com phrase `aerolink-tcc-victor-2026`
4. UART baud → 400000 → SAVE
5. Plug nos 4 fios do ESP32 FC v2 (V/G/T/R)
6. Liga tudo → bind deve fechar em segundos
7. Sniffer ESP32-C3 deve mostrar `[LINKED] ok=N crescendo`

Toda config do Heltec V2 já está pronta — só vai precisar do RX funcional.

---

## 🤝 Crédito

Debug colaborativo realizado durante o upgrade ELRS da Fase 1 do OpenRC AeroLink em 2026. Aprendizado documentado pra preservar conhecimento e poupar tempo de futuros maintainers ou estudantes que enfrentem cenários similares.
