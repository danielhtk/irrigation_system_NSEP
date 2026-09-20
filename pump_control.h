/* pump_control.h - hysteresis + pump safety */

#ifndef PUMP_CONTROL_H
#define PUMP_CONTROL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void setPump(bool on);
void enforcePumpSafety(void);
void runControlLogic(void);

/* Firmware watering timer (WATER:<secs>). Capped at PUMP_MAX_RUN_MS.
   Any pump stop clears the deadline. */
void startTimedWater(unsigned long secs);
unsigned int timedWaterLeft(void);   /* seconds left, 0 when none */

#ifdef __cplusplus
}
#endif

#endif /* PUMP_CONTROL_H */
