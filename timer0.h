/*
 * timer0.h - 4x-baud tick that clocks the bit-bang BLE UART.
 *
 * Timer0 is free because the Arduino core's millis() does not exist here.
 * 16 MHz / 8 / 52 = 38461 Hz, which is 4 x 9600 to within 0.16%.
 */

#ifndef TIMER0_H
#define TIMER0_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TIMER0_SUCCESS   0
#define TIMER0_ERROR  -1000

/* The swuart ring buffers are shared between this ISR and the main loop, so
 * the ISR has to be maskable around those updates. */
#define TIMER0_ISR_Disable()  (TIMSK0 = 0x00)
#define TIMER0_ISR_Enable()   (TIMSK0 = 0x02)

extern int32_t TIMER0_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* TIMER0_H */
