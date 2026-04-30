# NANO_RX_v2.0

Firmware atualizado do receptor utilizando Arduino Nano e módulo nRF24L01.

## Conexões e Pinagem

### nRF24L01 -> Arduino Nano RX
| nRF24L01 | Arduino Nano |
|----------|--------------|
| VCC      | 3.3V         |
| GND      | GND          |
| CE       | D8           |
| CSN      | D7           |
| SCK      | D13          |
| MOSI     | D11          |
| MISO     | D12          |

*(Nota: Utilize um adaptador para o nRF24L01 ou um regulador 3.3V adequado, o pino 3.3V do Nano pode não fornecer corrente suficiente).*

### Servos e Motores -> Arduino Nano RX
| Pino Nano | Função (Mix OFF - Normal) | Função (Mix ON - Zagi)   |
|-----------|---------------------------|--------------------------|
| **D2**    | Aileron                   | Elevon Esquerdo (L)      |
| **D3**    | Profundor                 | Elevon Direito (R)       |
| **D4**    | Acelerador (ESC)          | Acelerador (ESC)         |
| **D5**    | Leme                      | Leme                     |
| **D6**    | Extra (Desligado)         | Extra (Desligado)        |

## Controles e Switches no NANO_TX
* **S1 (Switch 1):** Liga ou desliga o mix Zagi. `1 = ON (Zagi)`, `0 = OFF (Normal)`.
* **S2 (Switch 2):** Corte de motor de segurança. `1 = Motor Cortado`, `0 = Motor Ativo`.
