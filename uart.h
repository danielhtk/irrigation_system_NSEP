/*
 * uart.h - USART0, the USB debug channel.
 *
 * TX is polled from the main loop, RX runs in an ISR. That split is
 * deliberate: DHT11_Read blocks for about 25 ms and the USART receive buffer
 * is only two bytes deep, so a polled RX would lose every keystroke typed
 * during a climate read. The reference project polls both and can afford it
 * only because its main loop never blocks.
 */

#ifndef UART_H
#define UART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UART_SUCCESS             0
#define UART_ERROR_TX_BUF_FULL -100
#define UART_ERROR_RX_BUF_FULL -101
#define UART_ERROR_RX_BUF_EMPTY -102
#define UART_ERROR_ARG         -103

/* Both rings are counted by a uint8_t, so the size must be at most 128: a
   uint8_t cannot represent 256 distinct fill levels, and the "is it full"
   test in UART_Write would be constant false. */
#define UART_BUF_TX_SIZE        64   /* must be a power of two */
#define UART_BUF_RX_SIZE        16   /* must be a power of two */

extern uint16_t UART_RX_OVERRUN;   /* bytes dropped because the RX ring was full */

extern int32_t UART_Init(void);
extern int32_t UART_Process(void);          /* call once per main-loop pass */
extern int32_t UART_Write(uint8_t value);
extern int32_t UART_Write_String(const char *str, uint16_t len);
extern int32_t UART_Write_All(const char *str, uint16_t len);  /* blocking, fatal path only */
extern int32_t UART_Read(uint8_t *byte);     /* 0 on success, < 0 when empty */
extern int32_t UART_Buffer_Clear(void);

#ifdef __cplusplus
}
#endif

#endif /* UART_H */
