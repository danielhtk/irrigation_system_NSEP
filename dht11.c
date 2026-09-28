/* dht11.c - DHT11 driver. Timer1 at 2 MHz (0.5 us/tick).
 * Bit HIGH window is measured with IRQs off; bus idles high on pull-up. */

#include "global.h"

#include <avr/interrupt.h>
#include <avr/io.h>

/* Prescaler 8 on a 16 MHz part: 2 MHz, 0.5 us per count. */
#define DHT11_TICKS_PER_US  2U

/* Bits decoded by last read, for diagnostics. */
static uint8_t dht11_last_bits = 0;

uint8_t DHT11_LastBits(void) {
  return dht11_last_bits;
}

static void DHT11_Timer1_Init(void) {
  TCCR1A = 0x00;
  TCCR1B = (uint8_t)(1 << CS11);   /* CTC, prescaler 8 */
  TCNT1  = 0x0000;
  OCR1A  = 0xFFFF;                 /* free running: TOP is never reached */
}

/* Wrap-safe microseconds elapsed since a TCNT1 snapshot. */
static uint16_t DHT11_Us_Since(uint16_t start) {
  return (uint16_t)(((uint16_t)(TCNT1 - start)) / DHT11_TICKS_PER_US);
}

/* Busy-wait ms. */
static void DHT11_Delay_Ms(uint16_t ms) {
  uint16_t start = TCNT1;
  uint16_t limit = (uint16_t)(ms * (uint16_t)(F_CPU / 8000UL));

  while ((uint16_t)(TCNT1 - start) < limit) {
    /* spin */
  }
}

/* Wait for the bus to reach `level`, or give up after `timeout_us`. */
static int32_t DHT11_Wait_Level(uint8_t level, uint16_t timeout_us) {
  uint16_t start = TCNT1;

  for (;;) {
    if (GPIO_Read(PIN_DHT) == level) {
      return DHT11_SUCCESS;
    }
    if (DHT11_Us_Since(start) > timeout_us) {
      return DHT11_ERROR_TIMEOUT;
    }
  }
}

/* Measure one bit's HIGH width and classify it. */
static int32_t DHT11_Read_Bit(uint8_t *bit) {
  uint16_t start, width;
  uint8_t saved, stuck = 0;

  if (bit == NULL) {
    return DHT11_ERROR_ARG;
  }

  /* End of TLOW; IRQs on here. */
  if (DHT11_Wait_Level(GPIO_HIGH, DHT11_EDGE_TIMEOUT_US) != DHT11_SUCCESS) {
    return DHT11_ERROR_TIMEOUT;
  }

  start = TCNT1;
  saved = SREG;
  cli();
  while (GPIO_Read(PIN_DHT) == GPIO_HIGH) {
    if (DHT11_Us_Since(start) >= DHT11_T1_MAX_US) {
      stuck = 1;
      break;
    }
  }
  width = DHT11_Us_Since(start);
  SREG = saved;

  if (stuck != 0) {
    return DHT11_ERROR_TIMEOUT;
  }

  *bit = (width > DHT11_BIT_THRESHOLD_US) ? 1u : 0u;

  return DHT11_SUCCESS;
}

int32_t DHT11_Init(void) {
  DHT11_Timer1_Init();

  /* Bus idle is high: keep pull-up on. */
  SYS_ERROR_CHECK(GPIO_Mode(PIN_DHT, GPIO_INPUT_PULLUP));

  return DHT11_SUCCESS;
}

int32_t DHT11_Read(float *tempC, float *humidityPct) {
  uint8_t data[5];
  uint8_t bit, index;
  int32_t result;

  if (tempC == NULL || humidityPct == NULL) {
    return DHT11_ERROR_ARG;
  }

  data[0] = 0; data[1] = 0; data[2] = 0; data[3] = 0; data[4] = 0;
  dht11_last_bits = 0;

  /* Drive low to wake sensor. Pre-load latch before output mode. */
  SYS_ERROR_CHECK(GPIO_Write(PIN_DHT, GPIO_LOW));
  SYS_ERROR_CHECK(GPIO_Mode(PIN_DHT, GPIO_OUTPUT));
  DHT11_Delay_Ms(DHT11_START_LOW_MS);

  /* Release bus to pull-up. */
  SYS_ERROR_CHECK(GPIO_Mode(PIN_DHT, GPIO_INPUT_PULLUP));

  /* Sensor pulls low, then high. */
  result = DHT11_Wait_Level(GPIO_LOW, DHT11_RESPONSE_TIMEOUT_US);
  if (result != DHT11_SUCCESS) {
    return result;
  }
  result = DHT11_Wait_Level(GPIO_HIGH, DHT11_RESPONSE_TIMEOUT_US);
  if (result != DHT11_SUCCESS) {
    return result;
  }

  /* Skip tail of Treh so first bit is not misread. */
  result = DHT11_Wait_Level(GPIO_LOW, DHT11_RESPONSE_TIMEOUT_US);
  if (result != DHT11_SUCCESS) {
    return result;
  }

  /* 40 bits, most significant first. */
  for (index = 0; index < 5; index++) {
    for (bit = 0; bit < 8; bit++) {
      uint8_t value = 0;

      result = DHT11_Read_Bit(&value);
      if (result != DHT11_SUCCESS) {
        return result;
      }
      dht11_last_bits++;
      /* MSB first: shift left, OR new bit. */
      data[index] = (uint8_t)((uint8_t)(data[index] << 1) | value);
    }
  }

  /* Bus free again, idling high on the pull-up. */
  SYS_ERROR_CHECK(GPIO_Mode(PIN_DHT, GPIO_INPUT_PULLUP));

  if (data[4] != (uint8_t)(data[0] + data[1] + data[2] + data[3])) {
    return DHT11_ERROR_CHECKSUM;
  }

  /* humidity integer + decimal */
  *humidityPct = (float)data[0] + ((float)data[1] * 0.1f);

  /* Bit 7 of byte 3 is the sign. */
  if ((data[3] & 0x80) != 0) {
    *tempC = -((float)data[2] + ((float)(data[3] & 0x7F) * 0.1f));
  } else {
    *tempC = (float)data[2] + ((float)data[3] * 0.1f);
  }

  return DHT11_SUCCESS;
}
