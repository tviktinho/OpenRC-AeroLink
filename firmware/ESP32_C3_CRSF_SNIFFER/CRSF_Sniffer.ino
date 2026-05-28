/* =============================================================================
 * CRSF Sniffer — ESP32-C3 mini
 *
 * Firmware minimalista pra validar o link ELRS: recebe CRSF do RX 915M na UART1
 * e imprime os 8 primeiros canais na Serial USB. Não tem PWM, mixagem ou
 * failsafe — é só um "monitor visual" do que o RX está mandando.
 *
 * Caso de uso: testar bind/link com Heltec sem precisar do FC v2 completo.
 *              Se aqui aparecer "ok=N crescendo + valores mudando", o bind
 *              está funcional e o problema (se houver) está na fiação do FC.
 *
 * PINOUT do ESP32-C3 mini:
 *   ┌──────────────────────────────────────────────┐
 *   │  ELRS 915M RX           ESP32-C3 mini        │
 *   │  ───────────            ──────────           │
 *   │  V  (5V) ─────────────► 5V                   │
 *   │  G       ─────────────  GND                  │
 *   │  T  (TX) ─────────────► GPIO 2  (UART1 RX)   │
 *   │  R  (RX) ◄────────────  GPIO 3  (UART1 TX)   │ (opcional, telemetria)
 *   └──────────────────────────────────────────────┘
 *
 *   ⚠️ GPIO 18/19 do ESP32-C3 são USB nativos — NÃO usar.
 *   ⚠️ GPIO 8 tem strapping de boot — evitar.
 *   ⚠️ Sem divisor de tensão: ambos lados são 3.3V nativo. Conexão direta OK.
 *
 * UART:
 *   - Serial (USB CDC) — 115200 baud, output humano
 *   - Serial1 (HW UART1) — 400000 baud, recebe CRSF do RX
 *
 * Autor: OpenRC-AeroLink, validação ELRS Fase 1
 * ============================================================================= */

#include <Arduino.h>
#include "crsf_rx.h"

// =============================================================================
// PINAGEM (ESP32-C3 mini)
// =============================================================================
constexpr uint8_t PIN_CRSF_RX = 2;   // ← TX do RX 915M
constexpr uint8_t PIN_CRSF_TX = 3;   // → RX do RX 915M (opcional)

// =============================================================================
// OBJETOS
// =============================================================================
CrsfRx crsf;

// =============================================================================
// HELPERS
// =============================================================================
inline uint8_t crsf_ch_to_pct(uint16_t ch) {
    // Mapeia 172..1811 → 0..100% para visualização rápida
    if (ch < CRSF_CHANNEL_VALUE_MIN) ch = CRSF_CHANNEL_VALUE_MIN;
    if (ch > CRSF_CHANNEL_VALUE_MAX) ch = CRSF_CHANNEL_VALUE_MAX;
    return (uint8_t)(((uint32_t)(ch - CRSF_CHANNEL_VALUE_MIN) * 100UL)
                     / (CRSF_CHANNEL_VALUE_MAX - CRSF_CHANNEL_VALUE_MIN));
}

void printBar(uint8_t pct, uint8_t width = 20) {
    // Barra ASCII tipo [████████░░░░░░░░░░░░]
    Serial.print('[');
    uint8_t filled = (pct * width) / 100;
    for (uint8_t i = 0; i < width; i++) {
        Serial.print(i < filled ? '#' : '.');
    }
    Serial.print(']');
}

// =============================================================================
// SETUP
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(2000);  // dá tempo pro USB CDC enumerar

    Serial.println();
    Serial.println(F("================================================"));
    Serial.println(F("  CRSF Sniffer — ESP32-C3 mini"));
    Serial.println(F("  OpenRC AeroLink, validação ELRS Fase 1"));
    Serial.println(F("================================================"));
    Serial.printf("CRSF UART1: RX=GPIO%d TX=GPIO%d @ %lu baud\n",
                  PIN_CRSF_RX, PIN_CRSF_TX, (unsigned long)CRSF_BAUDRATE);

    // ESP32-C3 tem 2 UARTs hardware utilizáveis:
    //   Serial  = USB CDC (não conta como UART RF)
    //   Serial1 = UART1 hardware (vamos usar pra CRSF)
    Serial1.begin(CRSF_BAUDRATE, SERIAL_8N1, PIN_CRSF_RX, PIN_CRSF_TX);

    Serial.println(F("Aguardando CRSF do RX 915M..."));
    Serial.println(F("Mexa os pots do controle pra ver canais mudando."));
    Serial.println();
}

// =============================================================================
// LOOP
// =============================================================================
void loop() {
    // 1. Alimenta o parser com bytes da UART
    while (Serial1.available()) {
        crsf.feed((uint8_t)Serial1.read());
    }

    // 2. Print periódico (10 Hz) com estado dos canais
    static uint32_t last_print_ms = 0;
    uint32_t now = millis();
    if (now - last_print_ms >= 100) {
        last_print_ms = now;

        bool linked = (now - crsf.lastFrameMs() < 200);
        Serial.print(linked ? "[LINKED] " : "[NO LINK] ");
        Serial.printf("ok=%lu bad=%lu | ",
                      (unsigned long)crsf.goodFrames(),
                      (unsigned long)crsf.badFrames());

        // 8 primeiros canais como barras + valor em µs
        for (uint8_t i = 0; i < 4; i++) {
            uint16_t ch  = crsf.getChannel(i);
            uint16_t us  = crsf_channel_to_us(ch);
            uint8_t  pct = crsf_ch_to_pct(ch);
            Serial.printf("CH%d=%4dus ", i + 1, us);
            printBar(pct, 10);
            Serial.print(' ');
        }
        Serial.println();
    }

    // 3. Failsafe visual: se passar >500ms sem frame, alerta
    static bool was_linked = false;
    bool linked_now = (now - crsf.lastFrameMs() < 500);
    if (was_linked && !linked_now) {
        Serial.println();
        Serial.println(F(">>> LINK PERDIDO — RX parou de enviar CRSF <<<"));
        Serial.println();
    } else if (!was_linked && linked_now) {
        Serial.println();
        Serial.println(F(">>> LINK ESTABELECIDO — CRSF chegando! <<<"));
        Serial.println();
    }
    was_linked = linked_now;
}
