/* telemetry.c - DATA/ALERT lines. No %f on avr-libc: one decimal by hand.
 * Never blocks: a full BLE ring counts a drop and retries next cycle. */

#include "global.h"

#include <math.h>    /* isnan */
#include <stdio.h>   /* snprintf */

static const char CRLF[] = "\r\n";

/* One decimal; NaN prints as NA. */
static void fmt_float_1(char *text, size_t capacity, float value) {
  int   negative, whole, tenth;
  float magnitude;

  if (capacity == 0) {
    return;
  }
  if (isnan(value)) {
    snprintf(text, capacity, "NA");
    return;
  }
  negative = (value < 0.0f) ? 1 : 0;
  magnitude = negative ? -value : value;
  whole = (int)magnitude;
  tenth = (int)((magnitude - (float)whole) * 10.0f + 0.5f);
  if (tenth >= 10) { whole += 1; tenth = 0; }
  snprintf(text, capacity, "%s%d.%d", negative ? "-" : "", whole, tenth);
}

/* "Not connected" / "ring full" are normal drops, not faults. */
static int32_t ble_normalize(int32_t result) {
  if (result == SWUART_ERROR_NOT_CONNECTED ||
      result == SWUART_ERROR_TX_BUF_FULL) {
    return SWUART_SUCCESS;
  }
  return result;
}

/* USB drops are trace only, never fatal. */
static int32_t uart_normalize(int32_t result) {
  return (result == UART_ERROR_TX_BUF_FULL) ? UART_SUCCESS : result;
}

int32_t bleWriteRaw(const char *text) {
  if (text == NULL) {
    return SWUART_ERROR_ARG;
  }
  return ble_normalize(SWUART_Write_String(text, (uint16_t)strlen(text)));
}

int32_t bleWriteLine(const char *text) {
  int32_t result;

  if (text == NULL) {
    return SWUART_ERROR_ARG;
  }
  result = SWUART_Write_String(text, (uint16_t)strlen(text));
  if (SWUART_Write_String(CRLF, (uint16_t)sizeof(CRLF) - 1) == SWUART_ERROR_TX_BUF_FULL) {
    result = SWUART_ERROR_TX_BUF_FULL;
  }
  return ble_normalize(result);
}

int32_t debugWriteLine(const char *text) {
  uint16_t len;
  int32_t result;

  if (text == NULL) {
    return UART_ERROR_ARG;
  }
  len = (uint16_t)strlen(text);

  /* Burst-proof: pump the USB ring until the line fits instead of dropping
   * it. Steady-state traffic never spins (the ~1 ms/byte drain keeps up); a
   * DIAG burst can stall here up to ~1 ms per backlogged byte. Dropping the
   * content while its CRLF still fits prints a blank line, which looks like
   * a missing reply. Callers must keep lines under UART_BUF_TX_SIZE, or this
   * spins forever; the longest current caller is 64 bytes. ISRs stay enabled
   * throughout, so the BLE link and SYS_TICK keep running while we wait. */
  for (;;) {
    result = UART_Write_String(text, len);
    if (result != UART_ERROR_TX_BUF_FULL) {
      break;
    }
    UART_Process();
  }
  for (;;) {
    result = UART_Write_String(CRLF, (uint16_t)sizeof(CRLF) - 1);
    if (result != UART_ERROR_TX_BUF_FULL) {
      break;
    }
    UART_Process();
  }
  return uart_normalize(result);
}

int32_t bleWriteLineMirrored(const char *text) {
  bleWriteLine(text);
  return debugWriteLine(text);
}

int32_t sendAlert(const char *code) {
  char line[32];

  strncpy(line, "ALERT,", 6);
  strncpy(line + 6, (code != NULL) ? code : "?", sizeof(line) - 6);
  line[sizeof(line) - 1] = '\0';

  bleWriteLine(line);
  return debugWriteLine(line);
}

int32_t sendTelemetry(void) {
  char line[52];
  char tempText[8], humText[8];
  int32_t result;

  fmt_float_1(tempText, sizeof(tempText), tempC);
  fmt_float_1(humText, sizeof(humText), humPct);

  /* Effective observed rain for backward compatibility */
  bool effectiveRain = rainNow || rainAuto;

  snprintf(line, sizeof(line), "DATA,%d,%d,%d,%d,%d,%s,%s,%u,%u,%d",
           soilPct,
           effectiveRain ? 1 : 0,
           pumpState ? 1 : 0,
           sensorFault ? 1 : 0,
           activeCount,
           tempText, humText,
           (unsigned)probeMap,
           (unsigned)timedWaterLeft(),
           autoMode ? 1 : 0);
  line[sizeof(line) - 1] = '\0';

  bleWriteLine(line);
  result = debugWriteLine(line);

  return result;
}
