/*
 * swuart.h - bit-bang UART for the BLE module.
 *
 * Serviced from the Timer0 ISR at 4x the bit rate, so the module keeps
 * receiving correctly even while the main loop is blocked inside a DHT11 read
 * (about 25 ms). That is the whole reason for doing this in an ISR rather than
 * polling: a polled bit-bang receiver loses sync during any long operation.
 *
 * Pin directions follow the module's point of view, using the PIN_BLE_TX /
 * PIN_BLE_RX entries from default_config.h so there is one pin map:
 *   PIN_BLE_TX - Arduino TX, drives the module's RX
 *   PIN_BLE_RX - Arduino RX, driven by the module's TX
 */

#ifndef SWUART_H
#define SWUART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SWUART_SUCCESS                0
#define SWUART_ERROR_TX_BUF_FULL  -2000
#define SWUART_ERROR_RX_BUF_EMPTY -2001
#define SWUART_ERROR_ARG          -2002
#define SWUART_ERROR_NOT_CONNECTED -2003

#define SWUART_TX_BUF_SIZE           64   /* must be a power of two */
#define SWUART_RX_BUF_SIZE          128   /* must be a power of two: a 25 ms
                                           * DHT11 read accumulates ~24 bytes
                                           * at 9600 baud while the main loop
                                           * is blocked, so 16 overruns on
                                           * every climate read */

extern uint8_t  SWUART_TX_BYTES;   /* readable between ISR passes */
extern uint8_t  SWUART_RX_BYTES;
extern uint16_t SWUART_TX_DROPPED; /* writes refused: the TX ring was full */
extern uint16_t SWUART_RX_OVERRUN; /* bytes lost: the RX ring was full */
extern uint16_t SWUART_RX_FRAMING; /* stop-bit errors */

extern int32_t SWUART_Init(void);

extern int32_t SWUART_Connected(void);

extern int32_t SWUART_Write_Byte(uint8_t byte);
extern int32_t SWUART_Write_String(const char *str, uint16_t len);
extern int32_t SWUART_Read_Byte(uint8_t *byte);

/* Called from the Timer0 ISR once per quarter-bit tick. */
extern void SWUART_Process(void);

#ifdef __cplusplus
}
#endif

#endif /* SWUART_H */
