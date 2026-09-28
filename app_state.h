/*
 * app_state.h - mutable state shared between modules, defined in app_state.c.
 *
 * Every timestamp is a SYS_TICK value (milliseconds, free-running). Compare
 * them as signed differences so the 49-day rollover is harmless:
 *
 *     if ((int32_t)(SYS_TICK_Now() - deadline) >= 0) { ... }
 */

#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdbool.h>
#include <stdint.h>

#define RX_BUF_SIZE  32
#define RX_BUF_MAX   30   /* extras dropped */

#ifdef __cplusplus
extern "C" {
#endif

/* Voted soil */
extern int  soilPct;
extern int  activeCount;
extern bool sensorFault;

/* Exclusion mask, bit i = probe i not voting (failed checks or demoted).
   8 probes max. */
extern uint8_t probeMap;

/* Control */
extern bool pumpState;
extern bool autoMode;
extern bool rainNow;            /* manual latch: RAIN:ON/OFF sets this */
extern bool rainAuto;           /* auto-detected: humidity rule sets this */
extern bool rainAwareEnabled;   /* master switch: config RAIN_AWARE_ENABLED */
extern int  threshLow;          /* pump on below this */
extern int  threshHigh;         /* pump off at/above this */

/* Forecast state (server -> firmware) */
extern bool forecastReceived;
extern uint32_t lastRainfMs;

/* Firmware watering timer: SYS_TICK deadline, 0 = no timed run. */
extern uint32_t waterDeadline;

/* Climate (NAN = no reading yet) */
extern float tempC;
extern float humPct;

/* Scheduler timestamps */
extern uint32_t lastSense;
extern uint32_t lastReport;
extern uint32_t lastDht;
extern uint32_t bootMs;          /* SYS_TICK at the end of SYS_Init */
extern uint32_t pumpStartMs;
extern uint32_t cooldownUntil;

/* Incoming BLE line */
extern char    rxBuffer[RX_BUF_SIZE];
extern uint8_t rxLen;

#ifdef __cplusplus
}
#endif

#endif /* APP_STATE_H */
