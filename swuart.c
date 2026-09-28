/* swuart.c - bit-bang UART, 8N1, 9600 baud. ISR at 4x baud (40 ticks/frame).
 * RX samples at bit centre (ticks 2, 6+4k, 38). Counters run from start edge;
 * reloading per state shifts sampling to edges and breaks framing. */

#include "global.h"

static uint8_t SWUART_TX_BUF[SWUART_TX_BUF_SIZE];
static uint8_t SWUART_TX_BUF_WR;     /* main loop only */
static uint8_t SWUART_TX_BUF_RD;     /* ISR only */
uint8_t SWUART_TX_BYTES;             /* both: updated inside a critical section */

static uint8_t SWUART_RX_BUF[SWUART_RX_BUF_SIZE];
static uint8_t SWUART_RX_BUF_WR;     /* ISR only */
static uint8_t SWUART_RX_BUF_RD;     /* main loop only */
uint8_t SWUART_RX_BYTES;             /* both: updated inside a critical section */

uint16_t SWUART_TX_DROPPED;
uint16_t SWUART_RX_OVERRUN;
uint16_t SWUART_RX_FRAMING;

typedef enum {
  SWUART_IDLE,
  SWUART_START,
  SWUART_DATA,
  SWUART_STOP
} SWUART_STATE_e;

static SWUART_STATE_e TX_STATE;
static SWUART_STATE_e RX_STATE;
static uint8_t tx_byte, tx_tick;
static uint8_t rx_byte, rx_bit, rx_tick;

/* Port registers cached for the 38.5 kHz ISR (no pin lookup per sample). */
static volatile uint8_t *tx_port;
static uint8_t tx_mask;
static volatile uint8_t *rx_port;
static uint8_t rx_mask;

/* ISR-context line access. */
static inline void swuart_drive_tx(uint8_t level) {
  if (level != 0) {
    *tx_port |= tx_mask;
  } else {
    *tx_port &= (uint8_t)~tx_mask;
  }
}

static inline uint8_t swuart_sample_rx(void) {
  return ((*rx_port & rx_mask) != 0) ? GPIO_HIGH : GPIO_LOW;
}

/* Bit mask for a pin on its port. */
static inline uint8_t swuart_pin_mask(uint8_t pin) {
  return (uint8_t)(1u << (pin & 7));
}

int32_t SWUART_Init(void) {
  TX_STATE = SWUART_IDLE;
  RX_STATE = SWUART_IDLE;
  tx_byte = 0; tx_tick = 0;
  rx_byte = 0; rx_bit = 0; rx_tick = 0;

  TIMER0_ISR_Disable();          /* hold off the ISR while the rings are reset */

  SWUART_TX_BUF_WR = 0; SWUART_TX_BUF_RD = 0; SWUART_TX_BYTES = 0;
  SWUART_RX_BUF_WR = 0; SWUART_RX_BUF_RD = 0; SWUART_RX_BYTES = 0;
  SWUART_TX_DROPPED = 0; SWUART_RX_OVERRUN = 0; SWUART_RX_FRAMING = 0;

  TIMER0_ISR_Enable();

  /* Idle is high, so set the level before switching the pin to output. */
  SYS_ERROR_CHECK(GPIO_Write(PIN_BLE_TX, GPIO_HIGH));
  SYS_ERROR_CHECK(GPIO_Mode(PIN_BLE_TX, GPIO_OUTPUT));
  /* Weak pull-up: keeps the line defined when the module is unplugged, and is
   * far too weak to fight the module's own driver when it is connected. */
  SYS_ERROR_CHECK(GPIO_Mode(PIN_BLE_RX, GPIO_INPUT_PULLUP));
  /* STATE pin. Pulled up so an unwired or unpowered module reads "connected"
   * and telemetry still flows; a module that drives the pin overrides it. */
  SYS_ERROR_CHECK(GPIO_Mode(PIN_BLE_STATUS, GPIO_INPUT_PULLUP));

  /* Cache the port registers for the ISR. */
  if (PIN_BLE_TX < 8) {
    tx_port = &PORTD;
  } else {
    tx_port = &PORTB;
  }
  tx_mask = swuart_pin_mask(PIN_BLE_TX);

  if (PIN_BLE_RX < 8) {
    rx_port = &PIND;
  } else {
    rx_port = &PINB;
  }
  rx_mask = swuart_pin_mask(PIN_BLE_RX);

  return SWUART_SUCCESS;
}

int32_t SWUART_Connected(void) {
  if (GPIO_Read(PIN_BLE_STATUS) != GPIO_HIGH) {
    return 0;
  }
  return 1;
}

int32_t SWUART_Write_Byte(uint8_t byte) {
  int32_t result = SWUART_SUCCESS;

  if (SWUART_Connected() == 0) {
    return SWUART_ERROR_NOT_CONNECTED;
  }

  TIMER0_ISR_Disable();
  if (SWUART_TX_BYTES < SWUART_TX_BUF_SIZE) {
    SWUART_TX_BUF[SWUART_TX_BUF_WR] = byte;
    SWUART_TX_BUF_WR = (uint8_t)((SWUART_TX_BUF_WR + 1) & (SWUART_TX_BUF_SIZE - 1));
    SWUART_TX_BYTES++;
  } else {
    SWUART_TX_DROPPED++;
    result = SWUART_ERROR_TX_BUF_FULL;
  }
  TIMER0_ISR_Enable();

  return result;
}

int32_t SWUART_Write_String(const char *str, uint16_t len) {
  uint16_t i;
  int32_t result = SWUART_SUCCESS;

  if (str == NULL) {
    return SWUART_ERROR_ARG;
  }

  for (i = 0; i < len; i++) {
    int32_t rc = SWUART_Write_Byte((uint8_t)str[i]);

    if (rc == SWUART_ERROR_NOT_CONNECTED) {
      /* Link state, not a ring problem: stop here and say so, so the caller
       * can tell "nobody is listening" from "the buffer is full". */
      return SWUART_ERROR_NOT_CONNECTED;
    }
    if (rc != SWUART_SUCCESS) {
      result = SWUART_ERROR_TX_BUF_FULL;
    }
  }

  return result;
}

int32_t SWUART_Read_Byte(uint8_t *byte) {
  if (byte == NULL) {
    return SWUART_ERROR_ARG;
  }

  TIMER0_ISR_Disable();
  if (SWUART_RX_BYTES == 0) {
    TIMER0_ISR_Enable();
    return SWUART_ERROR_RX_BUF_EMPTY;
  }

  *byte = SWUART_RX_BUF[SWUART_RX_BUF_RD];
  SWUART_RX_BUF_RD = (uint8_t)((SWUART_RX_BUF_RD + 1) & (SWUART_RX_BUF_SIZE - 1));
  SWUART_RX_BYTES--;
  TIMER0_ISR_Enable();

  return SWUART_SUCCESS;
}

/* Push a byte the ISR just received. ISR context, no locking needed. */
static void SWUART_Rx_Push(uint8_t value) {
  if (SWUART_RX_BYTES < SWUART_RX_BUF_SIZE) {
    SWUART_RX_BUF[SWUART_RX_BUF_WR] = value;
    SWUART_RX_BUF_WR = (uint8_t)((SWUART_RX_BUF_WR + 1) & (SWUART_RX_BUF_SIZE - 1));
    SWUART_RX_BYTES++;
  } else {
    SWUART_RX_OVERRUN++;
  }
}

void SWUART_Process(void) {
  /* ---------------- transmit ----------------
   * Continuous count from the tick the start bit is driven: start occupies
   * ticks 0..3, bit k ticks 4+4k..7+4k, stop ticks 36..39. */
  switch (TX_STATE) {
    case SWUART_IDLE:
      if (SWUART_TX_BYTES > 0) {
        tx_byte = SWUART_TX_BUF[SWUART_TX_BUF_RD];
        SWUART_TX_BUF_RD = (uint8_t)((SWUART_TX_BUF_RD + 1) & (SWUART_TX_BUF_SIZE - 1));
        SWUART_TX_BYTES--;
        tx_tick = 1;
        swuart_drive_tx(GPIO_LOW);                 /* start bit */
        TX_STATE = SWUART_START;
      }
      break;

    case SWUART_START:
      if (tx_tick == 3) {                            /* one full bit period */
        TX_STATE = SWUART_DATA;
      }
      tx_tick++;
      break;

    case SWUART_DATA:
      if (tx_tick == 35) {
        TX_STATE = SWUART_STOP;
      } else if ((tx_tick & 3) == 0) {
        swuart_drive_tx((uint8_t)(tx_byte & 0x01));  /* LSB first */
        tx_byte >>= 1;
      }
      tx_tick++;
      break;

    case SWUART_STOP:
      swuart_drive_tx(GPIO_HIGH);
      if (tx_tick == 39) {
        TX_STATE = SWUART_IDLE;
      }
      tx_tick++;
      break;
  }

  /* ---------------- receive ----------------
   * Same continuous count: start verified at tick 2, data sampled at the
   * centre of each bit (ticks 6+4k), stop sampled at tick 38. */
  switch (RX_STATE) {
    case SWUART_IDLE:
      if (swuart_sample_rx() == GPIO_LOW) {    /* idle is high */
        rx_byte = 0;
        rx_bit = 0;
        rx_tick = 1;
        RX_STATE = SWUART_START;
      }
      break;

    case SWUART_START:
      if (rx_tick == 2) {                            /* centre of the start bit */
        if (swuart_sample_rx() == GPIO_LOW) {
          RX_STATE = SWUART_DATA;
        } else {
          RX_STATE = SWUART_IDLE;                    /* false start */
        }
      }
      rx_tick++;
      break;

    case SWUART_DATA:
      if ((rx_tick & 3) == 2) {                      /* centre of a data bit */
        if (swuart_sample_rx() == GPIO_HIGH) {
          rx_byte |= (uint8_t)(1u << rx_bit);
        }
        rx_bit++;
        if (rx_bit >= 8) {
          RX_STATE = SWUART_STOP;   /* counter keeps running into the stop bit */
        }
      }
      rx_tick++;
      break;

    case SWUART_STOP:
      if (rx_tick == 38) {                           /* centre of the stop bit */
        if (swuart_sample_rx() == GPIO_HIGH) {
          SWUART_Rx_Push(rx_byte);
        } else {
          SWUART_RX_FRAMING++;   /* bad stop: drop the byte, resync below */
        }
      } else if (rx_tick >= 39) {
        RX_STATE = SWUART_IDLE;
      }
      rx_tick++;
      break;
  }
}
