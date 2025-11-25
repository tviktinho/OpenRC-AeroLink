/**
 * NANO RECEIVER - PPM GERADO POR INTERRUPÇÃO (SEM JITTER)
 * * Saída PPM: Pino D9 (Obrigatório usar D9 ou D10 com esta biblioteca Timer1)
 * Entrada: nRF24L01
 */

 #include <SPI.h>
 #include <RF24.h>
 
 // === CONFIGURAÇÕES RÁDIO ===
 #define CE_PIN   8  // Verifique sua solda!
 #define CSN_PIN  7 
 
 RF24 radio(CE_PIN, CSN_PIN);
 const byte ADDRESS[6] = "00001";
 const uint8_t CHANNEL = 76;
 
 struct __attribute__((packed)) PacketRF { 
   uint8_t p[8]; 
   uint8_t s1;   
   uint8_t s2;   
 } pkt;
 
 // === CONFIGURAÇÕES PPM ===
 #define PPM_PIN 9       // ATENÇÃO: Use o pino D9 para saída PPM neste código!
 #define CHANNEL_CNT 8
 
 // Valores dos canais (em ticks de timer, 1 tick = 0.5us com prescaler 8)
 volatile uint16_t ppm[CHANNEL_CNT];
 
 void setup() {
   // Configura Pino PPM
   pinMode(PPM_PIN, OUTPUT);
 
   // --- CONFIGURAÇÃO DO TIMER 1 PARA PPM ---
   // Isso gera o sinal automaticamente no fundo, sem travar o loop
   noInterrupts();
   TCCR1A = 0;
   TCCR1B = 0;
   TCNT1  = 0;
   OCR1A  = 4000;  // Inicia com um valor qualquer (2ms)
   TCCR1B |= (1 << WGM12);  // CTC mode
   TCCR1B |= (1 << CS11);   // 8 prescaler: 0.5 microsegundos por tick
   TIMSK1 |= (1 << OCIE1A); // Habilita interrupção timer compare
   interrupts();
 
   // Valores Iniciais (Centro)
   for(int i=0; i<CHANNEL_CNT; i++) ppm[i] = 3000; // 1500us * 2 ticks
   ppm[2] = 2000; // Throttle (1000us)
 
   // Inicia Rádio
   radio.begin();
   radio.setPALevel(RF24_PA_MAX);
   radio.setDataRate(RF24_250KBPS);
   radio.setChannel(CHANNEL);
   radio.setAutoAck(false);
   radio.openReadingPipe(0, ADDRESS);
   radio.startListening();
 }
 
 void loop() {
   // O loop fica 100% livre para ler o rádio o mais rápido possível
   if (radio.available()) {
     radio.read(&pkt, sizeof(pkt));
     
     // ATENÇÃO: A ordem aqui define qual stick mexe qual barra
     // Se estiver trocado, troque os índices [x] aqui ou no Betaflight
     
     // Ordem AETR (Aileron, Elevator, Throttle, Rudder)
     setChannel(1, map(pkt.p[2], 0, 255, 2000, 1000)); // Pitch
     setChannel(0, map(pkt.p[3], 0, 255, 1000, 2000)); // Roll
     setChannel(2, map(pkt.p[0], 0, 255, 1000, 2000)); // Throttle
     setChannel(3, map(pkt.p[1], 0, 255, 1000, 2000)); // Yaw
     
     // Switches
     setChannel(4, pkt.s1 ? 2000 : 1000); // AUX1
     setChannel(5, pkt.s2 ? 2000 : 1000); // AUX2
     
     setChannel(6, 1500);
     setChannel(7, 1500);
   }
   
   // Pequeno delay para estabilidade do rádio
   delay(1);
 }
 
 // Helper para converter us em ticks do timer
 void setChannel(int ch, int microseconds) {
   // Limita entre 1000 e 2000
   if (microseconds < 1000) microseconds = 1000;
   if (microseconds > 2000) microseconds = 2000;
   // Multiplica por 2 porque o timer roda a 0.5us por tick
   ppm[ch] = microseconds * 2;
 }
 
 // === INTERRUPÇÃO DO TIMER (O CORAÇÃO DO PPM) ===
 ISR(TIMER1_COMPA_vect) {
   static boolean state = true;
   static byte currentChannel = 0;
   static uint16_t calcPPM = 0;
   
   if (state) { // Pulso de Separação (300us)
     digitalWrite(PPM_PIN, HIGH); // Ou LOW se for invertido
     OCR1A = 600; // 300us * 2
     state = false;
   } else { // Comprimento do Canal
     digitalWrite(PPM_PIN, LOW); // Ou HIGH
     state = true;
     
     if (currentChannel >= CHANNEL_CNT) {
       // Sync Pulse (Tempo morto para completar 22.5ms)
       OCR1A = 45000 - calcPPM; 
       currentChannel = 0;
       calcPPM = 0;
     } else {
       // Pulso do Canal atual - 300us do separador
       OCR1A = ppm[currentChannel] - 600; 
       calcPPM += ppm[currentChannel];
       currentChannel++;
     }
   }
 }