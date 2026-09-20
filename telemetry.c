/* telemetry.c - DATA/ALERT lines over BLE + USB.
 * One decimal by hand: avr-libc printf has no %f on the Uno. */

#include "global.h"

#include <math.h>    /* isnan */
#include <stdio.h>   /* snprintf */

#define TELEM_MSG_SIZE 64

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

void sendAlert(const char *code) {
  char line[TELEM_MSG_SIZE];
  snprintf(line, sizeof(line), "ALERT,%s", (code != NULL) ? code : "?");
  line[sizeof(line) - 1] = '\0';
  ble_write_line(line);
  debug_write_line(line);
}

void sendTelemetry(void) {
  char line[TELEM_MSG_SIZE];
  char tempText[12], humText[12];

  fmt_float_1(tempText, sizeof(tempText), tempC);
  fmt_float_1(humText, sizeof(humText), humPct);

  /* Effective observed rain for backward compatibility */
  bool effectiveRain = rainNow || rainAuto;

  snprintf(line, sizeof(line), "DATA,%d,%d,%d,%d,%d,%s,%s,%u,%u",
           soilPct,
           effectiveRain ? 1 : 0,
           pumpState ? 1 : 0,
           sensorFault ? 1 : 0,
           activeCount,
           tempText, humText,
           (unsigned)probeMap,
           timedWaterLeft());
  line[sizeof(line) - 1] = '\0';

  ble_write_line(line);
  debug_write_line(line);
}
