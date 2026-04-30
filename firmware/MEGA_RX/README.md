# MEGA_RX

Firmware para Arduino Mega 2560 usado como ponte de um receptor RC PWM para o PC.

## Fluxo atual
- O Mega le 8 canais PWM diretamente nos pinos `A8` ate `A15`.
- Cada largura de pulso e convertida de `1000..2000 us` para `0..255`.
- O Mega envia uma linha CSV pela USB com `p0,p1,p2,p3,p4,p5,p6,p7`.

## Importante
- Receptor comercial de radio controle nao entrega sinal analogico continuo.
- A leitura correta e por largura de pulso PWM.
- Ligue o `GND` do receptor no `GND` do Mega.

## Pinagem
- `A8` -> canal 1
- `A9` -> canal 2
- `A10` -> canal 3
- `A11` -> canal 4
- `A12` -> canal 5
- `A13` -> canal 6
- `A14` -> canal 7
- `A15` -> canal 8

## Ajuste fino
- Se algum canal nao estiver chegando perto dos extremos, ajuste `CHANNEL_MIN_US` e `CHANNEL_MAX_US` em `MEGA_RX.ino`.
- Se algum eixo estiver invertido, ajuste `CHANNEL_INVERT`.
