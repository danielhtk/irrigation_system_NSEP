/*
 * config.h - pins, calibration, thresholds, timing
 * Tested wiring: relay on 7, DHT11 on 6. DHT uses the Adafruit DHT library.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>   /* for A0..A2 */
#include <stdint.h>

/* Pins */
#define PIN_RELAY   7   /* relay IN */
#define PIN_DHT     6
#define PIN_BLE_RX  2   /* Arduino RX <- BLE TX */
#define PIN_BLE_TX  3   /* Arduino TX -> BLE RX (use divider) */

/* A0..A2 are not macros in the AVR core, so index them via soil_pin().
   To add a probe: add PIN_SOIL_3, a case below, bump SOIL_SENSOR_COUNT. */
#define PIN_SOIL_0 A0
#define PIN_SOIL_1 A1
#define PIN_SOIL_2 A2
#define SOIL_SENSOR_COUNT 3

static inline uint8_t soil_pin(uint8_t probe) {
  switch (probe) {
    case 0:  return PIN_SOIL_0;
    case 1:  return PIN_SOIL_1;
    default: return PIN_SOIL_2;
  }
}

/* Relay polarity: 1 = active-low module. Set to 0 if pump runs when idle. */
#define RELAY_ACTIVE_LOW 1

/* Calibration - measure with your own sensors. Capacitive: HIGH = dry. */
#define SOIL_RAW_AIR    550   /* probe in dry air */
#define SOIL_RAW_WATER  10    /* probe in water */

/* Outside this window = unplugged/shorted, does not vote. */
#define SOIL_RAW_MIN  5
#define SOIL_RAW_MAX  620

/* Max jump across the 5 samples of one read. A floating (unplugged) pin
   wanders; a connected probe is steady. Raise if plugged probes get rejected. */
#define SOIL_SPREAD_MAX  50

/* Max % gap allowed between voting probes. Probes must sit in the same zone.
   Smaller flags drifters earlier; larger tolerates real soil variation. */
#define SOIL_SPLIT_MAX  25

/* Voted 0% means no data (floating probe also reads 0), never waters. */
#define SOIL_PCT_VALID_MIN 1

/* Hysteresis defaults, changeable at runtime via THRESH:low,high. */
#define THRESH_LOW_DEFAULT   20
#define THRESH_HIGH_DEFAULT  30

#define ALERT_DROUGHT_PCT  20
#define ALERT_FLOOD_PCT    85

/* Level alerts re-fire at most this often. Event alerts fire at once. */
#define LEVEL_ALERT_INTERVAL_MS 30000UL

/* Debug probes: 1 = on, 0 = off.
 * HC05 = AT console at boot (needs phone disconnected).
 * SNIFF = log every raw BLE byte. */
#define DEBUG_HC05_PROBE 0
#define DEBUG_BLE_SNIFF  0

/* Test mode: 1 = BLE loopback instead of pump control. */
#define TEST_BLE_LOOPBACK 0

/* Timing: all values in ms. seconds x 1000, minutes x 60000 (e.g. 5 min = 300000UL). */
#define SENSE_INTERVAL_MS       2000UL
#define REPORT_INTERVAL_MS      10000UL
#define DHT_INTERVAL_MS         5000UL     /* DHT11 needs >= 2 s */
#define PUMP_MAX_RUN_MS         300000UL   /* 5 min cutoff */
#define PUMP_COOLDOWN_MS        60000UL    /* post-cutoff cooldown only */
#define PUMP_MIN_RUN_MS         10000UL    /* 10 s minimum run (anti-short-cycle) */
#define PUMP_MIN_OFF_MS         120000UL   /* 2 min minimum off between auto runs */

/* Rain-aware watering */
#define RAIN_AWARE_ENABLED              1
#define RAIN_FORECAST_X_HOURS           12
#define RAIN_FORECAST_Y_MM              2
#define RAIN_HOLD_CAP_Z_PCT             25       /* between threshLow and threshHigh */
#define RAIN_HUMIDITY_SKIP_THRESH       80       /* % — skip if humidity > this */
#define RAIN_HUMIDITY_OBSERVED_THRESH   90       /* % — observed rain confirmation */
#define RAIN_EMERGENCY_FLOOR_PCT        15       /* % — below threshLow, triggers water to Z */
#define RAIN_FORECAST_TTL_MS            (2UL * 3600UL * 1000UL)  /* 2 hours */
#define RAIN_EVAL_INTERVAL_MS           60000UL  /* 1 minute */

/* 0% guard margin: SOIL_RAW_AIR = driest_real_reading + SOIL_RAW_AIR_MARGIN */
#define SOIL_RAW_AIR_MARGIN             50       /* ADC counts above driest real reading */

/* Auto-resume after fault: 0 = manual only (default), 1 = auto after N healthy cycles */
#define AUTO_RESUME_AFTER_FAULT         0
#define AUTO_RESUME_HEALTHY_READINGS    10   /* consecutive healthy sense cycles (2s each = 20s) */

#endif /* CONFIG_H */
