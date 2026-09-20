/*
 * app_state.h - globals owned by the sketch, defined in app_state.c.
 */

#ifndef APP_STATE_H
#define APP_STATE_H

#include <Arduino.h>   /* uint8_t, unsigned long */
#include <stdbool.h>

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
extern bool rainDetected;       /* legacy alias = rainNow */
extern bool rainNow;            /* manual latch: RAIN:ON/OFF sets this */
extern bool rainAuto;           /* auto-detected: humidity rule sets this */
extern bool rainAwareEnabled;   /* master switch: config RAIN_AWARE_ENABLED */
extern int  threshLow;          /* pump on below this */
extern int  threshHigh;         /* pump off at/above this */

/* Forecast state (server -> firmware) */
extern bool forecastReceived;
extern unsigned long lastRainfMs;

/* Firmware watering timer: sys-tick deadline, 0 = no timed run. */
extern unsigned long waterDeadline;

/* Climate (NAN = no reading yet) */
extern float tempC;
extern float humPct;

/* Timestamps */
extern unsigned long lastSense;
extern unsigned long lastReport;
extern unsigned long lastDht;
extern unsigned long pumpStartMs;
extern unsigned long cooldownUntil;

/* Incoming BLE line */
extern char    rxBuffer[RX_BUF_SIZE];
extern uint8_t rxLen;

#ifdef __cplusplus
}
#endif

#endif /* APP_STATE_H */
