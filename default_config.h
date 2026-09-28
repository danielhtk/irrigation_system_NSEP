/*
 * default_config.h - tracked defaults. Don't edit per-board values here;
 * copy the lines you need into config.h (gitignored, optional) instead.
 * Anything not overridden there keeps the default below.
 *
 * config.h is included FIRST, and every default is wrapped in #ifndef, so a
 * plain #define in config.h wins silently. The early include also matters for
 * the derived values below (RELAY_OFF_LEVEL / RELAY_ON_LEVEL), which are
 * computed from RELAY_ACTIVE_LOW and would otherwise see the default polarity
 * instead of the override.
 */

#ifndef DEFAULT_CONFIG_H
#define DEFAULT_CONFIG_H

#include <stdint.h>

/* Local overrides (optional, gitignored). */
#if defined(__has_include) && __has_include("config.h")
#include "config.h"
#endif

/* A0..A5 are digital pins 14..19, i.e. bits 0..5 of PORTC. Defined here
   rather than pulled from the Arduino core, which this firmware does not
   link. Guarded because the Arduino builder injects <Arduino.h> into the .ino,
   and an identical redefinition would otherwise be a diagnostic. */
#ifndef A0
#define A0 14
#define A1 15
#define A2 16
#define A3 17
#define A4 18
#define A5 19
#endif

/* Pins */
#ifndef PIN_RELAY
#define PIN_RELAY   7   /* relay IN */
#endif
#ifndef PIN_DHT
#define PIN_DHT     6
#endif
#ifndef PIN_BLE_RX
#define PIN_BLE_RX  2   /* Arduino RX <- BLE TX */
#endif
#ifndef PIN_BLE_TX
#define PIN_BLE_TX  3   /* Arduino TX -> BLE RX (use divider) */
#endif
#ifndef PIN_BLE_STATUS
#define PIN_BLE_STATUS 4 /* module STATE: HIGH while a client is connected.
                            Arduino_IoT (3) uses D6; D6 is the DHT11 here. */
#endif

/* To add a probe: add PIN_SOIL_3, a case below, bump SOIL_SENSOR_COUNT. */
#ifndef PIN_SOIL_0
#define PIN_SOIL_0 A0
#endif
#ifndef PIN_SOIL_1
#define PIN_SOIL_1 A1
#endif
#ifndef PIN_SOIL_2
#define PIN_SOIL_2 A2
#endif
#ifndef SOIL_SENSOR_COUNT
#define SOIL_SENSOR_COUNT 3
#endif

static inline uint8_t soil_pin(uint8_t probe) {
  switch (probe) {
    case 0:  return PIN_SOIL_0;
    case 1:  return PIN_SOIL_1;
    default: return PIN_SOIL_2;
  }
}

/* Relay polarity: 1 = active-low module. Set to 0 if pump runs when idle. */
#ifndef RELAY_ACTIVE_LOW
#define RELAY_ACTIVE_LOW 1
#endif

/* The level that de-energises the relay, and the one SYS_Init pre-loads into
 * the output latch before the pin is switched to output. Derived from the
 * polarity flag so the two can never disagree. Each is independently
 * overridable; otherwise derived from the (possibly overridden) polarity. */
#if !defined(RELAY_OFF_LEVEL) && !defined(RELAY_ON_LEVEL)
#if RELAY_ACTIVE_LOW
#define RELAY_OFF_LEVEL  GPIO_HIGH
#define RELAY_ON_LEVEL   GPIO_LOW
#else
#define RELAY_OFF_LEVEL  GPIO_LOW
#define RELAY_ON_LEVEL   GPIO_HIGH
#endif
#endif
#ifndef RELAY_OFF_LEVEL
#if RELAY_ACTIVE_LOW
#define RELAY_OFF_LEVEL  GPIO_HIGH
#else
#define RELAY_OFF_LEVEL  GPIO_LOW
#endif
#endif
#ifndef RELAY_ON_LEVEL
#if RELAY_ACTIVE_LOW
#define RELAY_ON_LEVEL   GPIO_LOW
#else
#define RELAY_ON_LEVEL   GPIO_HIGH
#endif
#endif

/* Calibration - measure with your own sensors. Capacitive: HIGH = dry. */
#ifndef SOIL_RAW_AIR
#define SOIL_RAW_AIR    506   /* probe in dry air */
#endif
#ifndef SOIL_RAW_WATER
#define SOIL_RAW_WATER  200    /* probe in water */
#endif

/* Outside this window = unplugged/shorted, does not vote. */
#ifndef SOIL_RAW_MIN
#define SOIL_RAW_MIN  150     /* reads ~0 when unplugged */
#endif
#ifndef SOIL_RAW_MAX
#define SOIL_RAW_MAX  550     /* reads 1023 when shorted */
#endif

/* Max jump across the samples of one read. A floating (unplugged) pin
   wanders; a connected probe is steady. Raise if plugged probes get rejected. */
#ifndef SOIL_SPREAD_MAX
#define SOIL_SPREAD_MAX  50
#endif

/* Samples averaged per probe per read. */
#ifndef SOIL_SAMPLE_COUNT
#define SOIL_SAMPLE_COUNT  5
#endif

/* Max % gap allowed between voting probes. Probes must sit in the same zone.
   Smaller flags drifters earlier; larger tolerates real soil variation. */
#ifndef SOIL_SPLIT_MAX
#define SOIL_SPLIT_MAX  25
#endif

/* Voted 0% means no data (floating probe also reads 0), never waters. */
#ifndef SOIL_PCT_VALID_MIN
#define SOIL_PCT_VALID_MIN 1
#endif

/* Hysteresis defaults, changeable at runtime via THRESH:low,high. */
#ifndef THRESH_LOW_DEFAULT
#define THRESH_LOW_DEFAULT   30
#endif
#ifndef THRESH_HIGH_DEFAULT
#define THRESH_HIGH_DEFAULT  70
#endif

#ifndef ALERT_DROUGHT_PCT
#define ALERT_DROUGHT_PCT  20
#endif
#ifndef ALERT_FLOOD_PCT
#define ALERT_FLOOD_PCT    85
#endif

/* Level alerts re-fire at most this often. Event alerts fire at once. */
#ifndef LEVEL_ALERT_INTERVAL_MS
#define LEVEL_ALERT_INTERVAL_MS 30000UL
#endif

/* Debug probes: 1 = on, 0 = off.
 * HC05 = AT console at boot (needs phone disconnected).
 * SNIFF = log every raw BLE byte. */
#ifndef DEBUG_HC05_PROBE
#define DEBUG_HC05_PROBE 0
#endif
#ifndef DEBUG_BLE_SNIFF
#define DEBUG_BLE_SNIFF  0
#endif

/* DIAG command: raw DHT11/soil/bandgap readings for hardware bring-up.
 * Costs ~1.1 kB flash and ~110 B SRAM, so set it back to 0 for release. */
#ifndef DEBUG_SENSOR_DIAG
#define DEBUG_SENSOR_DIAG 1
#endif

/* Test mode: 1 = BLE loopback instead of pump control. */
#ifndef TEST_BLE_LOOPBACK
#define TEST_BLE_LOOPBACK 0
#endif



/* Timing: all values in ms. seconds x 1000, minutes x 60000 (e.g. 5 min = 300000UL). */
#ifndef SENSE_INTERVAL_MS
#define SENSE_INTERVAL_MS       2000UL
#endif
#ifndef REPORT_INTERVAL_MS
#define REPORT_INTERVAL_MS      10000UL
#endif
#ifndef DHT_INTERVAL_MS
#define DHT_INTERVAL_MS         5000UL     /* DHT11 needs >= 2 s */
#endif
#ifndef PUMP_MAX_RUN_MS
#define PUMP_MAX_RUN_MS         300000UL   /* 5 min cutoff */
#endif
#ifndef PUMP_COOLDOWN_MS
#define PUMP_COOLDOWN_MS        60000UL    /* post-cutoff cooldown only */
#endif
#ifndef PUMP_MIN_RUN_MS
#define PUMP_MIN_RUN_MS         10000UL    /* 10 s minimum run (anti-short-cycle) */
#endif
#ifndef PUMP_MIN_OFF_MS
#define PUMP_MIN_OFF_MS         120000UL   /* 2 min minimum off between auto runs */
#endif

/* Rain-aware watering */
#ifndef RAIN_AWARE_ENABLED
#define RAIN_AWARE_ENABLED              1
#endif
#ifndef RAIN_FORECAST_X_HOURS
#define RAIN_FORECAST_X_HOURS           12
#endif
#ifndef RAIN_FORECAST_Y_MM
#define RAIN_FORECAST_Y_MM              2
#endif
#ifndef RAIN_HOLD_CAP_Z_PCT
#define RAIN_HOLD_CAP_Z_PCT             25       /* between threshLow and threshHigh */
#endif
#ifndef RAIN_HUMIDITY_SKIP_THRESH
#define RAIN_HUMIDITY_SKIP_THRESH       80       /* % — skip if humidity > this */
#endif
#ifndef RAIN_HUMIDITY_OBSERVED_THRESH
#define RAIN_HUMIDITY_OBSERVED_THRESH   90       /* % — observed rain confirmation */
#endif
#ifndef RAIN_EMERGENCY_FLOOR_PCT
#define RAIN_EMERGENCY_FLOOR_PCT        15       /* % — below threshLow, triggers water to Z */
#endif
#ifndef RAIN_FORECAST_TTL_MS
#define RAIN_FORECAST_TTL_MS            (2UL * 3600UL * 1000UL)  /* 2 hours */
#endif
#ifndef RAIN_EVAL_INTERVAL_MS
#define RAIN_EVAL_INTERVAL_MS           60000UL  /* 1 minute */
#endif

/* Auto-resume after fault: 0 = manual only (default), 1 = auto after N healthy cycles */
#ifndef AUTO_RESUME_AFTER_FAULT
#define AUTO_RESUME_AFTER_FAULT         1
#endif
#ifndef AUTO_RESUME_HEALTHY_READINGS
#define AUTO_RESUME_HEALTHY_READINGS    5   /* consecutive healthy sense cycles (2s each = 20s) */
#endif

#endif /* DEFAULT_CONFIG_H */
