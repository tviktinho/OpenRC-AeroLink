# OpenRC AeroLink — Protocolo de Comunicação

## Estrutura do Pacote (NRF24)
[ AX1 ][ AX2 ][ AX3 ][ AX4 ][ AX5 ][ AX6 ][ AX7 ][ AX8 ][ FLAGS ][ MODE ][ SEQ ][ CHK ]

- **AX1..AX8** → valores 0–255 (eixos analógicos).  
- **FLAGS** → bits para switches (S1, S2, reservas).  
- **MODE** → valor 0/1/2 (modos de controle) 
- **SEQ** → contador incremental de pacotes.  
- **CHK** → soma de verificação (mod 256).

## UART (TX → HUD ESP8266)
Formato CSV, exemplo:
ax1,ax2,ax3,ax4,ax5,ax6,ax7,ax8,s1,s2,mode

## Failsafe
- Timeout de pacotes → canais neutros.  
- HUD sinaliza “LINK LOST” + beep longo.  

## Calibração
- Botão `CAL` → TX envia comando especial, HUD mostra animação.  
- Valores podem ser gravados em EEPROM.  
