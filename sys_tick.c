/* sys_tick.c - Timer2 CTC 1 ms tick. Timer2 is free (tone() unused).
 * ISR only bumps the counter; all decisions stay in the main loop. */

#include "global.h"

#include <avr/interrupt.h>
#include <avr/io.h>

static volatile unsigned long tickCount = 0;

void sys_tick_begin(void) {
  TCCR2A = 0x02;   /* CTC mode */
  TCCR2B = 0x04;   /* prescaler 64 -> 16 MHz / 64 / 250 = 1 kHz */
  TCNT2  = 0x00;
  OCR2A  = 249;
  TIFR2  = 0x07;   /* clear pending flags */
  TIMSK2 = 0x02;   /* compare-match interrupt on */
}

ISR(TIMER2_COMPA_vect) {
  tickCount++;
}

unsigned long sys_tick_now(void) {
  unsigned long now;
  uint8_t saved = SREG;
  cli();
  now = tickCount;
  SREG = saved;
  return now;
}
