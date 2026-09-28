/*
 * gpio.h - register-level GPIO driver. No Arduino API: this firmware never
 * calls init(), so pinMode()/digitalWrite() do not exist.
 *
 * Pins 0-7 drive PORTD, 8-13 PORTB, 14-19 PORTC (the analog inputs).
 */

#ifndef GPIO_H
#define GPIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GPIO_SUCCESS        0
#define GPIO_ERROR_PIN     -1
#define GPIO_ERROR_MODE    -2
#define GPIO_ERROR_LEVEL   -3

#define GPIO_PIN_MAX       19

#define GPIO_INPUT         0
#define GPIO_OUTPUT        1

#define GPIO_LOW           0
#define GPIO_HIGH          1

/* Inputs with no external driver. The soil probes and the BLE RX line are
 * driven by other parts; the DHT11 bus carries its own pull-up. */
#define GPIO_INPUT_FLOAT   0
#define GPIO_INPUT_PULLUP  2

extern int32_t GPIO_Mode(uint8_t pin, uint8_t mode);
extern int32_t GPIO_Write(uint8_t pin, uint8_t level);
extern int32_t GPIO_Read(uint8_t pin);

/* GPIO_Mode(pin, GPIO_OUTPUT) touches DDR only, so the pin's output latch
 * keeps whatever GPIO_Write last stored. Pre-load the level before switching
 * to output, otherwise the pin drives whatever the latch happened to hold:
 *
 *     GPIO_Write(PIN_RELAY, RELAY_OFF_LEVEL);
 *     GPIO_Mode(PIN_RELAY, GPIO_OUTPUT);
 */

#ifdef __cplusplus
}
#endif

#endif /* GPIO_H */
