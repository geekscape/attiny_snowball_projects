/*
 * Simon game for ATTiny85 microcontroller
 *
 * Usage
 * ~~~~~
 * Press and release [Reset] button to begin playing Simon game
 *
 * Press and hold [?] button whilst press and release [Reset] button ...
 * - Top-left     [Orange]: Continue with the best scored game so far
 * - Top-right    [Yellow]: Replay the best scored game from the start
 * - Bottom-left  [Green]:  Demonstration mode ... random LEDs and tones
 * - Bottom-right [Red]:    Best scored game is erased, start from fresh
 *
 * Device programming
 * ~~~~~~~~~~~~~~~~~~
 * make clean
 * make flash  # Flash memory: 1,198 out of 2,048 bytes used (58 %)
 */

#if 0
#define EMULATOR 1
#define WATCHDOG 0  // TODO: Diagnose Timer and Watchdog code not working
#include <TinyDebug.h>
#else
#define WATCHDOG 1  // Timer and Watchdog code works on the actual ATTiny85 IC
#endif

#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

#include <stdint.h>
#include <util/delay_basic.h>

const uint8_t buttons[4] = { 0x0a, 0x06, 0x03, 0x12 };
const uint8_t tones[4]   = { 239, 179, 143, 119 };

uint32_t context;
uint8_t  last_button;
uint8_t  level = 0;
uint8_t  max_level;
uint16_t seed;

volatile uint8_t nrot = 8;
volatile uint16_t time;

void sleep_now() {
  PORTB = 0x00;  // Disable all pull-up resistors
  cli();         // Disable all interrupts
  WDTCR = 0;     // Turn off the Watchdog timer
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sleep_cpu();
}

void play(uint8_t index, uint16_t period) {
  PORTB  = 0x00;            // Set all pins low, disable pull-up resistors
  DDRB   = buttons[index];  // Set speaker and button[index] pin as output
  OCR0A  = tones[index];
  OCR0B  = tones[index] >> 1;
  TCCR0B = (1 << WGM02) | (1 << CS01); // Prescaler / 8
  _delay_loop_2(period);
  TCCR0B = 0x00;            // Clear clock source, i.e Timer0 stopped
  DDRB   = 0x00;
  PORTB  = 0x1d;
}

void level_up() {
  for (uint8_t index = 0; index < 4; index ++)  play(index, 25000);
}

void game_over() {
  for (uint8_t index = 0; index < 4; index ++)  play(3 - index, 25000);

  if (level > max_level) {
    eeprom_write_byte((uint8_t*)  0, ~ level);  // Write best score
    eeprom_write_word((uint16_t*) 1,   seed);   // Write seed
    // Play best score melody
    for (uint8_t index = 0; index < 3; index ++) level_up();
  }
  sleep_now();
}

void reset_context() {
  context = seed;
}

uint8_t simple_random4() {
//context = 2053 * context + 13849;         // ATTiny13
  context = context * 1103515245 + 12345;   // ATTiny85
  uint8_t temp = context ^ (context >> 8);  // XOR two bytes
  temp ^= (temp >> 4);                      // XOR two nibbles
// XOR two pairs of bits and return remainder after division by 4
  return((temp ^ (temp >> 2)) & 0x03);
}

// 0=16ms, 1=32ms, 2=64ms, 3=128ms, 4=250ms, 5=500ms, 6=1s, 7=2s, 8=4s, 9=8s
void setup_watchdog(uint8_t period) {
  uint8_t lsb;
  if (period > 9) period = 9;
  lsb = period & 0x07;
  if (period > 7) lsb |= (1<<5);
  lsb |= (1<<WDCE);
  MCUSR &= ~(1<<WDRF);
  WDTCR |= (1<<WDCE) | (1<<WDE);  // Start timed sequence
  WDTCR = lsb;                    // Set new watchdog timeout value
  WDTCR |= _BV(WDIE);
} 

ISR(WDT_vect) {
  time ++;                        // Increase every 16 ms
  if (nrot) {
    nrot --;
    seed = (seed << 1) ^ TCNT0;   // Random seed generation
  }
}

#ifdef EMULATOR
void loop() {}

void setup(void) {
  Debug.begin();
  Debug.println(F("Reset"));
#else
int main(void) {
#endif
  PORTB = 0x1d; // Enable pull-up resistors on 4 game buttons

// Enable and start the conversion on unconnected ADC0 (ADMUX: 0x00 by default)
  ADCSRA |= (1 << ADEN) | (1 << ADSC);
  while (ADCSRA & (1 << ADSC));  // ADSC is cleared when the conversion finishes
  seed = ADCL;                   // Set seed to lower ADC byte
  ADCSRA = 0x00;                 // Turn off ADC (saves power)

  cli();
//WDTCR = (1 << WDTIE);  // ATTiny13
#if WATCHDOG
  setup_watchdog(0);     // Interrupt every 16ms
#endif
  sei();
  TCCR0B = (1 << CS00);  // Timer0 in normal mode (no prescaler)

#if WATCHDOG
  while (nrot);  // Repeat for first 8 WDT interrupts to shuffle the seed
#endif

// Set Timer0 to enable correct PWM
  TCCR0A = (1 << COM0B1) | (0 << COM0B0) | (0 << WGM01)  | (1 << WGM00);

  max_level = ~ eeprom_read_byte((uint8_t*) 0);  // Read best score from EEPROM

  switch (PINB & 0x1d) {
    case 0x19:  // Top left [Orange] button is pressed during reset
                // Start from max level and load seed from EEPROM
      level = max_level;
      // fall through
    case 0x1c:  // Top right [Yellow] button is pressed during reset
                // Load seed from EEPROM but start from first level
      seed = eeprom_read_word((uint16_t*) 1);
      break;

    case 0x0d:  // Bottom left [Green] button is pressed during reset
                // Play random tones in an infinite loop
      level = 255;
      break;

    case 0x15:  // Bottom right [Red] button is pressed during reset
                // Reset best score
      eeprom_write_byte((uint8_t*) 0, 255);
      max_level = 0;
      break;
  }

  while (1) {
    reset_context();
// never ends if level == 255
    for (uint8_t index = 0; index <= level; index ++) {
      _delay_loop_2(4400 + 489088 / (8 + level));
      play(simple_random4(), 45000);
    }
    time = 0;
    last_button = 5;
    reset_context();
    for (uint8_t index = 0; index <= level; index ++) {
      uint8_t pressed = 0;
      while (! pressed) {
        for (uint8_t button = 0; button < 4; button ++) {
          if (! (PINB & buttons[button] & 0x1d)) {
            if (time > 1 || button != last_button) {
              play(button, 45000);
              pressed = 1;
              uint8_t correct = simple_random4();
              if (button != correct) {
                for (uint8_t index2 = 0; index2 < 3; index2 ++) {
                  _delay_loop_2(10000);
                  play(correct, 20000);
                }
                _delay_loop_2(65535);
                game_over();
              }
              time = 0;
              last_button = button;
              break;
            }
            time = 0;
          }
        }
        if (time > 4000) sleep_now();
      }
    }
    _delay_loop_2(65535);
    if (level < 254) {
      level ++;
      level_up();
      _delay_loop_2(45000);
    }
    else {  // Special animation for highest allowable (255th) level
      level_up();
      game_over();
    }
  }
}
