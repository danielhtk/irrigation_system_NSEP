/* sys.c - board bring-up and fatal handler (relay off, halt). */

#include "global.h"

#include <stdio.h>
#include <string.h>

int32_t SYS_ERROR_CODE;

/* Drive relay to OFF level. Safe to call after any fault. */
static void SYS_Relay_Off(void) {
  GPIO_Write(PIN_RELAY, RELAY_OFF_LEVEL);
}

void SYS_FATAL_ERROR(int32_t err, int32_t line, const char *file) {
  char message[48];

  /* Pump off first; nothing below may depend on drivers. */
  cli();
  SYS_Relay_Off();

  if (file == NULL) {
    file = "?";
  }

  snprintf(message, sizeof(message), "FATAL ERROR %ld at line %ld in %s\r\n",
           (long)err, (long)line, file);
  message[sizeof(message) - 1] = '\0';

  UART_Write_All(message, (uint16_t)strlen(message));

  /* Halt: relay is off, message sent. No ISR work needed here. */
  for (;;) {
    /* halted */
  }
}

int32_t SYS_Init(void) {
  /* Relay first, level before direction (no ON blip). */
  SYS_Relay_Off();
  SYS_ERROR_CHECK(GPIO_Mode(PIN_RELAY, GPIO_OUTPUT));
  SYS_ERROR_CHECK(GPIO_Write(PIN_RELAY, RELAY_OFF_LEVEL));

  /* Hardware UART before anything that might want to report. */
  SYS_ERROR_CHECK(UART_Init());
  SYS_ERROR_CHECK(TIMER2_Init());

  /* BLE UART before the timer that drives it. */
  SYS_ERROR_CHECK(SWUART_Init());
  SYS_ERROR_CHECK(TIMER0_Init());

  SYS_ERROR_CHECK(ADC_Init());
  SYS_ERROR_CHECK(DHT11_Init());

  {
    uint8_t probe;
    for (probe = 0; probe < SOIL_SENSOR_COUNT; probe++) {
      SYS_ERROR_CHECK(GPIO_Mode(soil_pin(probe), GPIO_INPUT));
    }
  }

  SYS_ERROR_CHECK(setPump(false));

  SYS_ERROR_CODE = SYS_SUCCESS;

  return SYS_SUCCESS;
}
