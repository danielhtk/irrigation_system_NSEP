/* timer2.c - 1 ms system tick. Timer2 CTC, 16 MHz / 64 / 250 = 1 kHz.
 *
 * 32-bit rather than the reference project's 64-bit counter: a 32-bit tick
 * wraps after 49 days, and a 64-bit increment is an 8-byte read-modify-write
 * inside an ISR whose result readers cannot latch atomically.
 */

#include "global.h"

#include <avr/interrupt.h>
#include <avr/io.h>

volatile uint32_t SYS_TICK;

int32_t TIMER2_Init(void) {
  TCCR2A = (uint8_t)(1 << WGM21);   /* CTC, TOP = OCR2A */
  TCCR2B = (uint8_t)((1 << CS22));   /* prescaler 64 */
  TCNT2  = 0x00;
  OCR2A  = 249;
  TIFR2  = 0x07;                    /* clear every pending flag */
  TIMSK2 = (uint8_t)(1 << OCIE2A);  /* compare-match interrupt on */

  return TIMER2_SUCCESS;
}

ISR(TIMER2_COMPA_vect) {
  SYS_TICK++;
}

uint32_t SYS_TICK_Now(void) {
  uint32_t now;
  uint8_t saved = SREG;

  cli();
  now = SYS_TICK;
  SREG = saved;                     /* restore, so a caller inside cli() stays off */

  return now;
}

void TIMER2_ISR_Disable(void) {
  TIMSK2 = 0x00;
}

void TIMER2_ISR_Enable(void) {
  TIMSK2 = (uint8_t)(1 << OCIE2A);
}
