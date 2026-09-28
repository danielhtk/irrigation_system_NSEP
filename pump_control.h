/*
 * pump_control.h - hysteresis, safety cutoff, rain-aware decision, timed runs.
 *
 * Every entry point returns int32_t (0 on success). All timing is SYS_TICK.
 */

#ifndef PUMP_CONTROL_H
#define PUMP_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int32_t setPump(bool on);
extern int32_t enforcePumpSafety(void);
extern int32_t runControlLogic(void);

/* Call when the user takes manual ownership (PUMP/AUTO commands) so a pending
 * fault-recovery resume does not override an explicit choice. */
extern void pump_disarmAutoResume(void);

/* Firmware watering timer (WATER:<secs>). Capped at PUMP_MAX_RUN_MS.
   Any pump stop clears the deadline. */
extern int32_t startTimedWater(uint32_t secs);
extern uint16_t timedWaterLeft(void);   /* seconds left, 0 when none */

#ifdef __cplusplus
}
#endif

#endif /* PUMP_CONTROL_H */
