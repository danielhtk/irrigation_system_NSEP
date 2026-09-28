/* app_state.c - shared mutable state, defined once. */

#include "global.h"

#include <math.h>   /* NAN */

/* Start faulted with no reading (-1). Pump stays off until a vote arrives.
   Never init to 0: 0 reads as dry. */
int  soilPct     = -1;
int  activeCount = 0;
bool sensorFault = true;
uint8_t probeMap = (uint8_t)((1u << SOIL_SENSOR_COUNT) - 1u);   /* nothing voting yet */

/* Control state */
bool pumpState        = false;
bool autoMode         = true;
bool rainNow          = false;   /* manual latch */
bool rainAuto         = false;   /* auto-detected by humidity rule */
bool rainAwareEnabled = RAIN_AWARE_ENABLED;    /* master switch from config */
int  threshLow        = THRESH_LOW_DEFAULT;
int  threshHigh       = THRESH_HIGH_DEFAULT;

/* Forecast state */
bool forecastReceived = false;
uint32_t lastRainfMs = 0;
uint32_t waterDeadline = 0;

/* Climate state */
float tempC  = NAN;
float humPct = NAN;

/* Scheduler / safety timestamps */
uint32_t lastSense     = 0;
uint32_t lastReport    = 0;
uint32_t lastDht       = 0;
uint32_t bootMs        = 0;
uint32_t pumpStartMs   = 0;
uint32_t cooldownUntil = 0;

/* Incoming BLE line buffer */
char    rxBuffer[RX_BUF_SIZE] = { 0 };
uint8_t rxLen = 0;
