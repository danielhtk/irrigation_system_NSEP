/* climate_sensor.c - DHT11 read through the register-level driver.
 *
 * The sensor is skipped for the first second after boot: the datasheet says it
 * needs that long out of its unstable start-up state, and a read before then
 * returns a zeroed frame.
 *
 * A failed read leaves tempC / humPct untouched, so a disconnected sensor shows
 * up as stale data rather than as a sudden change that the rain logic would
 * act on.
 */

#include "global.h"

int32_t readDht(void) {
  float temp = 0.0f, hum = 0.0f;
  int32_t result;

  if ((int32_t)(SYS_TICK_Now() - bootMs) < (int32_t)DHT11_MIN_INTERVAL_MS) {
    return DHT11_SUCCESS;   /* warming up: not an error, keep the old values */
  }

  /* Defer while the BLE link is active. Each DHT bit masks the Timer0 ISR
   * for up to ~90 us (~3 missed 4x-baud ticks), which stretches whatever bit
   * the bit-bang UART is currently driving and shifts the receive sample
   * grid for bytes arriving mid-read. Waiting for an idle link costs at most
   * one scheduler cycle; the next pass retries. Bytes that still arrive
   * mid-read can mis-sample, but the receiver resynchronises on the next
   * start bit and counts SWUART_RX_FRAMING. */
  if (SWUART_TX_BYTES > 0 || SWUART_RX_BYTES > 0) {
    return DHT11_SUCCESS;   /* link busy: try again next cycle, keep old values */
  }

  result = DHT11_Read(&temp, &hum);
  if (result != DHT11_SUCCESS) {
    return result;                /* keep the old values */
  }

  tempC  = temp;
  humPct = hum;

  return DHT11_SUCCESS;
}
