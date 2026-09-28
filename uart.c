/* uart.c - USB debug channel, 9600 baud. TX is polled; RX is ISR-fed. */

#include "global.h"

#include <avr/interrupt.h>
#include <avr/io.h>

/* 16 MHz / (16 * (103 + 1)) = 9615 baud, +0.16% against 9600. */
#define UART_UBRR0L  103

static uint8_t TX_BUF[UART_BUF_TX_SIZE];
static uint8_t TX_BUF_WR;
static uint8_t TX_BUF_RD;
static uint8_t TX_BUF_BYTES;   /* 0..127, so the full test below is meaningful */

static uint8_t RX_BUF[UART_BUF_RX_SIZE];
static uint8_t RX_BUF_WR;
static uint8_t RX_BUF_RD;
static uint8_t RX_BUF_BYTES;

uint16_t UART_RX_OVERRUN;

int32_t UART_Init(void) {
  UBRR0H = 0x00;
  UBRR0L = UART_UBRR0L;
  UCSR0A = 0x00;                          /* U2X0 off: 9600 is within 2% either way */
  UCSR0B = (uint8_t)((1 << RXEN0) | (1 << TXEN0) | (1 << RXCIE0));
  UCSR0C = (uint8_t)((1 << UCSZ01) | (1 << UCSZ00));   /* 8 data bits, no parity, 1 stop */

  return UART_Buffer_Clear();
}

int32_t UART_Buffer_Clear(void) {
  uint8_t sreg = SREG;

  cli();
  TX_BUF_WR = 0;
  TX_BUF_RD = 0;
  TX_BUF_BYTES = 0;
  RX_BUF_WR = 0;
  RX_BUF_RD = 0;
  RX_BUF_BYTES = 0;
  UART_RX_OVERRUN = 0;
  SREG = sreg;

  return UART_SUCCESS;
}

ISR(USART_RX_vect) {
  uint8_t value = (uint8_t)UDR0;         /* reading UDR0 clears RXC */

  if (RX_BUF_BYTES < UART_BUF_RX_SIZE) {
    RX_BUF[RX_BUF_WR] = value;
    RX_BUF_WR = (uint8_t)((RX_BUF_WR + 1) & (UART_BUF_RX_SIZE - 1));
    RX_BUF_BYTES++;
  } else {
    UART_RX_OVERRUN++;                   /* counted, not silently dropped */
  }
}

int32_t UART_Process(void) {
  if (TX_BUF_BYTES > 0) {
    if ((UCSR0A & (uint8_t)(1 << UDRE0)) != 0) {
      UDR0 = TX_BUF[TX_BUF_RD];
      TX_BUF_RD = (uint8_t)((TX_BUF_RD + 1) & (UART_BUF_TX_SIZE - 1));
      TX_BUF_BYTES--;
    }
  }

  return UART_SUCCESS;
}

int32_t UART_Write(uint8_t value) {
  if (TX_BUF_BYTES >= UART_BUF_TX_SIZE) {
    return UART_ERROR_TX_BUF_FULL;
  }

  TX_BUF[TX_BUF_WR] = value;
  TX_BUF_WR = (uint8_t)((TX_BUF_WR + 1) & (UART_BUF_TX_SIZE - 1));
  TX_BUF_BYTES++;

  return UART_SUCCESS;
}

int32_t UART_Write_String(const char *str, uint16_t len) {
  uint16_t i;

  if (str == NULL) {
    return UART_ERROR_ARG;
  }
  if ((uint16_t)(TX_BUF_BYTES + len) > UART_BUF_TX_SIZE) {
    return UART_ERROR_TX_BUF_FULL;
  }

  for (i = 0; i < len; i++) {
    TX_BUF[TX_BUF_WR] = (uint8_t)str[i];
    TX_BUF_WR = (uint8_t)((TX_BUF_WR + 1) & (UART_BUF_TX_SIZE - 1));
  }
  TX_BUF_BYTES = (uint8_t)(TX_BUF_BYTES + len);

  return UART_SUCCESS;
}

/* Pushes the whole string, spinning on UART_Process() until the TX ring has
 * room. Only for the fatal path, which has no main loop left to pump. */
int32_t UART_Write_All(const char *str, uint16_t len) {
  uint16_t sent = 0;

  if (str == NULL) {
    return UART_ERROR_ARG;
  }

  while (sent < len) {
    UART_Process();
    if (TX_BUF_BYTES < UART_BUF_TX_SIZE) {
      uint16_t room = (uint16_t)(UART_BUF_TX_SIZE - TX_BUF_BYTES);
      uint16_t chunk = (uint16_t)(len - sent);

      if (chunk > room) {
        chunk = room;
      }
      if (chunk > 64) {
        chunk = 64;      /* keep draining the ring while we fill it */
      }
      if (UART_Write_String(str + sent, chunk) == UART_SUCCESS) {
        sent = (uint16_t)(sent + chunk);
      }
    }
  }

  /* let the last byte reach the shift register */
  while ((UCSR0A & (uint8_t)(1 << TXC0)) == 0) {
    UART_Process();
  }

  return UART_SUCCESS;
}

int32_t UART_Read(uint8_t *byte) {
  uint8_t sreg;

  if (byte == NULL) {
    return UART_ERROR_ARG;
  }

  /* Save/restore rather than sei() so this is also correct if it is ever
   * called with interrupts already off. */
  sreg = SREG;
  cli();
  if (RX_BUF_BYTES == 0) {
    SREG = sreg;
    return UART_ERROR_RX_BUF_EMPTY;
  }
  *byte = RX_BUF[RX_BUF_RD];
  RX_BUF_RD = (uint8_t)((RX_BUF_RD + 1) & (UART_BUF_RX_SIZE - 1));
  RX_BUF_BYTES--;
  SREG = sreg;

  return UART_SUCCESS;
}
