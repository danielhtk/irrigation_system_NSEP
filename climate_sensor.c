/* climate_sensor.c - DHT11 read. Keeps old values on failure. */

#include "global.h"

void readDht(void) {
  float temp = 0.0f, hum = 0.0f;
  if (dht_read(&temp, &hum)) {
    tempC  = temp;
    humPct = hum;
  }
}
