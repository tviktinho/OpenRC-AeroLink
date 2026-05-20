# Heltec V2 + ELRS — Troubleshooting

Problemas comuns no setup do upgrade ELRS e como diagnosticar.

---

## 🔴 Build no Configurator falha

### "Error: target not found"
- Verifique que selecionou **DIY 900 ESP32 SX127x RFM95 TX via UART** (não a versão "via JR" nem "via SPI").
- O JSON do `heltec_v2_sx1276.json` precisa estar colado no campo **Custom Hardware Layout** antes do Build.

### "Build failed: undefined reference radio_rxen"
- Algumas versões do ELRS exigem `radio_rxen`/`radio_txen` mesmo quando não tem PA externo.
- Solução: no JSON, adicione `"radio_rxen": -1, "radio_txen": -1` (o `-1` desabilita).

### "Cannot open COM port"
- Driver CP2102 não instalado → baixe em https://www.silabs.com/.
- Outro programa usando a porta (Arduino IDE, PlatformIO Monitor) → feche.
- No Linux/Mac, talvez precise dar `sudo chmod 666 /dev/ttyUSB0`.

---

## 🟡 Flash OK mas Heltec não dá sinal de vida

### OLED apagada após boot
- Cheque conexão USB (cabo de dados, não só de carga).
- Pressione o botão **RST** da placa.
- Verifique se selecionou **Regulatory Domain 915** (Brasil) ou 868 (UE) — se ficou 433 ou 2400 o firmware não bate com o SX1276.

### LED não pisca
- Pode estar OK mas o GPIO 25 não foi mapeado corretamente. Cheque o JSON.
- Algumas revisões antigas do Heltec V2 têm o LED em GPIO 22 ou 25 — confira a serigrafia da placa.

### Não aparece o WiFi `ExpressLRS TX`
- Os primeiros 60 s após boot são a janela do AP. Se passou desse tempo sem conectar nenhum RC, ELRS desliga o WiFi.
- Pressione o botão **PRG** (GPIO 0) 3 vezes seguidas para reativar.
- Ou reinicie a placa (RST).

---

## 🟡 OLED ligada mas mostra coisa estranha

### "ERR: NO HARDWARE"
- O Configurator achou que rodaria mas o boot detectou pinos errados. Confira:
  - `radio_nss = 18`, `radio_rst = 14`, `radio_dio0 = 26` no JSON.
  - Reconfigure e flashe de novo.

### "RADIO INIT FAIL"
- SX1276 não respondeu via SPI. Causas possíveis:
  - JSON com pinos SPI trocados (NSS=18, SCK=5, MISO=19, MOSI=27).
  - Placa com defeito de fábrica (raro).
  - Alimentação USB instável (use porta direta no PC, não em hub passivo).

### Tela "rolando" ou caractere quebrado
- `screen_type` errado no JSON. Para Heltec V2, use `1` (SSD1306 128x64).
- Se ainda assim quebrar, tente sem screen (`"screen_type": 0`) — só sai a UI mas o link continua.

---

## 🟡 ELRS roda mas RX não binda

### RX comprado é 2.4 GHz, não 900 MHz
- **Não vai bindar nunca.** São rádios físicos diferentes (SX1280 vs SX1276).
- Compre um RX 900 MHz (EPW6, SuperD 900, R24-D 900).

### Binding phrase diferente
- TX e RX precisam ter sido flashados com a **mesma frase**.
- Reflashe o RX com a mesma frase usada no TX.

### RX já bindado em outro TX
- Reinicie o RX em bind mode: ligue/desligue 3x rápido em <2s cada vez.
- Ou flashe com firmware ELRS de novo.

### Regulatory Domain divergente
- TX em 915 (BR/EUA), RX em 868 (UE) → não falam.
- Reflashe ambos no **mesmo Regulatory Domain**.

---

## 🟢 Link funciona mas canais não respondem

### CH1..CH7 estão "congelados" em 992
- O Nano não está mandando CRSF. Verifique:
  - Nano alimentado e LED piscando (a cada ~500 ms).
  - TX0 do Nano conectado ao GPIO 17 do Heltec (não invertido!).
  - GND comum entre as placas.
  - Baud rate do firmware Nano = 420000 (não 115200, nem 38400).

### Canais "tremem" mas não acompanham os pots
- Mau contato no fio do TX0 ou GND.
- Cheque com o teste descrito em [`../TX_NANO_v2/README.md`](../TX_NANO_v2/README.md) → "Hex dump via segundo Arduino".

### CH8 fica constante
- Switches não estão sendo lidos (cabos soltos no Nano), ou
- Bits estão sendo escritos errado (debug: adicione `Serial.println(sw_mask)` no Nano _v2_, mas vai bagunçar o CRSF — só use em diagnóstico).

### Canais respondem mas com "lag" grande
- Heltec configurado em packet rate baixo (25 Hz, F1000). Mude para 50 Hz ou 100 Hz no web UI ELRS.
- 200 Hz é o máximo em 900 MHz.

---

## 🟢 Link OK mas alcance ruim

### Antena ausente ou errada
- O conector U.FL do Heltec **precisa ter uma antena de 900 MHz** plugada. Sem antena, o SX1276 pode danificar em TX.
- Antenas de 2.4 GHz (WiFi padrão) **não servem** para 900 MHz.
- Recomendado: antena omni 2-3 dBi para 868/915 MHz com conector U.FL.

### Potência muito baixa
- Padrão do firmware ELRS no DIY 900 é 100 mW. Suba para 250 mW ou 500 mW no web UI.
- Limite legal no Brasil: 1 W EIRP na faixa 902-928 MHz (RFB nº 506).

### Interferência
- Faixa 902-928 MHz é ISM (Wi-Fi Halow, Sigfox, LoRaWAN). Em ambiente urbano denso, prefira voar em campo aberto.

---

## 🔴 nRF24 antigo parou de funcionar

Lembrete: a Fase 1 **mantém** o nRF24 funcionando. Se quebrou:

- O Nano está com firmware **v2** (não v1 nem o ESP32 colocado por engano em `TX_NANO/`).
- Verifique no Nano v2 o LED piscando + `nrfReady` true (adicione print de debug temporário em `setup()`).
- Cheque alimentação 3.3V do nRF24 — capacitor 10-100 µF entre VCC/GND é obrigatório.

---

## 📞 Onde pedir ajuda

- ExpressLRS Discord: https://discord.gg/expresslrs (canais `#general-help` e `#diy`)
- Issues do OpenRC-AeroLink: GitHub do projeto
- Para problemas do Heltec especificamente: https://heltec.org/wifi_kit_series/
