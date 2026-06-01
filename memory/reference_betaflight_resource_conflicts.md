---
name: reference-betaflight-resource-conflicts
description: Resource conflicts no Betaflight — pinos alocados duplamente (PWM/PPM vs UART) bloqueiam SBUS silenciosamente.
metadata:
  type: reference
---

Aprendizado do projeto ([[project-nano-multiprotocol-drone-tx]]): no Betaflight, **vários `resource` podem apontar pro mesmo pino do MCU** simultaneamente (ex: `PWM 3 A10` E `SERIAL_RX 1 A10`). Isso não é erro de config — o target declara assim pra suportar múltiplos cenários (PWM RX antigo vs serial RX moderno).

**Sintoma:** SBUS chega no pad físico correto, polaridade certa, frame válido, mas a FC nunca decodifica os canais (aba Receiver fica estática). Quando o BF inicia, o driver de UART perde a disputa pro driver de timer PWM (ou fica em estado indefinido).

**Diagnóstico:** rodar `resource` no CLI e procurar pinos repetidos. Padrão Matek F411-RX:
- `PA02` = `PWM 1` + `SERIAL_TX 2`
- `PA03` = `PPM 1` + `SERIAL_RX 2`
- `PA09` = `PWM 2` + `SERIAL_TX 1`
- `PA10` = `PWM 3` + `SERIAL_RX 1`

**Correção:** desalocar PWM/PPM dos pinos UART:
```
resource PWM 1 NONE
resource PWM 2 NONE
resource PWM 3 NONE
resource PPM 1 NONE
save
```

**Why:** o `feature SERIAL_RX` sozinho NÃO desaloca PWM/PPM dos mesmos pinos. Precisa fazer manual.

**How to apply:** sempre que SBUS/CRSF não funcionar no BF mesmo com config aparentemente correta, rodar `resource` e procurar duplicações de pinos PA01–PA15 que tocam os UARTs. Pode salvar horas de debug.
