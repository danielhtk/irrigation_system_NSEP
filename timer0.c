/* timer0.c - 4x-baud tick for the bit-bang UART.
 *
 * OCR0A = 51 with a prescaler of 8 gives 16 MHz / 8 / 52 = 38461 Hz. The ISR
 * does no protocol work itself; it just calls the swuart state machine, which
 * must be serviced once per tick.
 */

#include "global.h"

#include <avr/interrupt.h>
#include <avr/io.h>

int32_t TIMER0_Init(void) {
  TCCR0A = (uint8_t)(1 << WGM01);   /* CTC, TOP = OCR0A */
  TCCR0B = (uint8_t)(1 << CS01);    /* prescaler 8 */
  OCR0A  = 51;
  TIFR0  = 0x07;                    /* clear every pending flag */
  TIMSK0 = (uint8_t)(1 << OCIE0A);

  return TIMER0_SUCCESS;
}

ISR(TIMER0_COMPA_vect) {
  SWUART_Process();
}
