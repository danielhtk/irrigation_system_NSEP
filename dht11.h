/*
 * dht11.h - single-wire DHT11 climate sensor, register level.
 *
 * Replaces the Adafruit DHT library. The library needs millis(),
 * delayMicroseconds() and pinMode() from the Arduino core, none of which exist
 * in this firmware because init() is never called, and its Timer0 use collides
 * with the bit-bang UART tick.
 *
 * Timing comes from Timer1, free-running at 2 MHz (0.5 us per count):
 *
 *   host start signal   bus low, >= 18 ms (typ 20, max 30)
 *   Trel                sensor low,  78..88 us
 *   Treh                sensor high, 80..92 us
 *   TLOW                50..58 us, the same for a 0 and a 1
 *   TH0                 23..27 us  -> bit 0
 *   TH1                 68..74 us  -> bit 1
 *
 * The HIGH width is what carries the data, so that is what gets measured; the
 * threshold sits at the midpoint of the 27..68 us gap.
 */

#ifndef DHT11_H
#define DHT11_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DHT11_SUCCESS            0
#define DHT11_ERROR_ARG       -600
#define DHT11_ERROR_TIMEOUT   -601
#define DHT11_ERROR_CHECKSUM  -602

/* Tbe: pull the bus low for at least 18 ms. */
#define DHT11_START_LOW_MS        20U
/* Trel and Treh are 92 us at worst; 200 us covers slow clones. */
#define DHT11_RESPONSE_TIMEOUT_US 200U
/* Longest gap between edges mid-frame is TLOW max 58 us, or TH1 max 74 us. */
#define DHT11_EDGE_TIMEOUT_US      150U
/* A high level this long means the line is stuck and the frame is lost. */
#define DHT11_T1_MAX_US             90U
/* Midpoint of the TH0 max (27) and TH1 min (68) gap. */
#define DHT11_BIT_THRESHOLD_US      45U
/* The datasheet requires at least 1 s between reads. */
#define DHT11_MIN_INTERVAL_MS    1000U

extern int32_t DHT11_Init(void);
extern int32_t DHT11_Read(float *tempC, float *humidityPct);
extern uint8_t DHT11_LastBits(void);

#ifdef __cplusplus
}
#endif

#endif /* DHT11_H */
