/* adc.c - ADC driver. /128 prescaler = 125 kHz, ~104 us per read. */

#include "global.h"

#include <avr/io.h>

int32_t ADC_Init(void) {
  ADMUX  = (uint8_t)(1 << REFS0);   /* AVCC with the AREF pin as the reference */
  ADCSRB = 0x00;                    /* no extended sampling, no auto trigger */
  DIDR0  = 0x3F;                    /* disable the digital buffers on A0..A5 */
  ADCSRA = (uint8_t)((1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0));   /* enable, single conversion, /128 */

  return ADC_SUCCESS;
}

int32_t ADC_Read(uint8_t channel, int16_t *result) {
  if (result == NULL) {
    return ADC_ERROR_ARG;
  }
  if (channel > ADC_CHANNEL_MAX) {
    return ADC_ERROR_CHANNEL;
  }

  /* REFS1:0 and ADHSR live in bits 7:5, the channel in bits 3:0. */
  ADMUX = (uint8_t)((ADMUX & 0xE0) | (channel & 0x07));
  ADCSRA |= (uint8_t)(1 << ADSC);   /* start conversion */

  while ((ADCSRA & (uint8_t)(1 << ADSC)) != 0) {
    /* ADSC clears itself when the conversion completes */
  }

  /* Right-adjusted: read ADCL first (locks pair), then ADCH. */
  {
    uint8_t low = ADCL;
    uint8_t high = ADCH;
    *result = (int16_t)((uint16_t)low | ((uint16_t)high << 8));
  }

  return ADC_SUCCESS;
}

/* Bandgap check: ~225 counts at 5 V AVCC. */
int32_t ADC_ReadBandgap(int16_t *result) {
  uint8_t saved;

  if (result == NULL) {
    return ADC_ERROR_ARG;
  }

  saved = ADMUX;
  /* Bandgap = MUX 0x1E. */
  ADMUX = (uint8_t)((1 << REFS0) | 0x1E);   /* AVCC ref, MUX4:0 = 11110 */
  ADCSRA |= (uint8_t)(1 << ADSC);
  while ((ADCSRA & (uint8_t)(1 << ADSC)) != 0) {
    /* wait */
  }
  /* ADCL first, then ADCH. */
  {
    uint8_t low = ADCL;
    uint8_t high = ADCH;
    *result = (int16_t)((uint16_t)low | ((uint16_t)high << 8));
  }
  ADMUX = saved;

  return ADC_SUCCESS;
}
