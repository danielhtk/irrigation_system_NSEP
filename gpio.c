/* gpio.c - GPIO driver. 0-7 -> PORTD, 8-13 -> PORTB, 14-19 -> PORTC. */

#include "global.h"

#include <avr/io.h>

/* Resolve pin to DDR/PORT/PIN + mask. */
static int32_t gpio_resolve(uint8_t pin, volatile uint8_t **ddr,
                            volatile uint8_t **port, volatile uint8_t **in,
                            uint8_t *mask) {
  if (pin > GPIO_PIN_MAX) {
    return GPIO_ERROR_PIN;
  }

  if (pin < 8) {                 /* D0..D7 */
    *ddr = &DDRD; *port = &PORTD; *in = &PIND; *mask = (uint8_t)(1u << pin);
  } else if (pin < 14) {         /* D8..D13 */
    pin -= 8;
    *ddr = &DDRB; *port = &PORTB; *in = &PINB; *mask = (uint8_t)(1u << pin);
  } else {                       /* A0..A5, bits 0..5 of PORTC */
    pin -= 14;
    *ddr = &DDRC; *port = &PORTC; *in = &PINC; *mask = (uint8_t)(1u << pin);
  }

  return GPIO_SUCCESS;
}

int32_t GPIO_Mode(uint8_t pin, uint8_t mode) {
  volatile uint8_t *ddr, *port, *in;
  uint8_t mask;

  if (mode > GPIO_INPUT_PULLUP) {
    return GPIO_ERROR_MODE;
  }
  if (gpio_resolve(pin, &ddr, &port, &in, &mask) != GPIO_SUCCESS) {
    return GPIO_ERROR_PIN;
  }

  if (mode == GPIO_INPUT_PULLUP) {
    *port |= mask;               /* pull-up is the PORT bit on AVR */
    *ddr &= (uint8_t)~mask;
  } else if (mode == GPIO_OUTPUT) {
    /* Keep latch: callers pre-load level with GPIO_Write before output. */
    *ddr |= mask;
  } else {
    /* GPIO_INPUT_FLOAT: no pull-up. */
    *ddr &= (uint8_t)~mask;
    *port &= (uint8_t)~mask;
  }

  return GPIO_SUCCESS;
}

int32_t GPIO_Write(uint8_t pin, uint8_t level) {
  volatile uint8_t *ddr, *port, *in;
  uint8_t mask;

  if (level > GPIO_HIGH) {
    return GPIO_ERROR_LEVEL;
  }
  if (gpio_resolve(pin, &ddr, &port, &in, &mask) != GPIO_SUCCESS) {
    return GPIO_ERROR_PIN;
  }

  if (level == GPIO_LOW) {
    *port &= (uint8_t)~mask;
  } else {
    *port |= mask;
  }

  return GPIO_SUCCESS;
}

int32_t GPIO_Read(uint8_t pin) {
  volatile uint8_t *ddr, *port, *in;
  uint8_t mask;

  if (gpio_resolve(pin, &ddr, &port, &in, &mask) != GPIO_SUCCESS) {
    return GPIO_ERROR_PIN;
  }

  /* Always >= 0, safe in SYS_ERROR_CHECK. */
  return ((*in & mask) != 0) ? GPIO_HIGH : GPIO_LOW;
}
