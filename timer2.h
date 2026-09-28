/*
 * timer2.h - the one time base: SYS_TICK, 1 ms, free-running.
 *
 * Replaces millis(). It is independent of the core because this firmware
 * never calls init(), so nothing else configures Timer2.
 *
 * SYS_TICK_Now() reads atomically, so always compare tick differences as
 * signed ints: (int32_t)(SYS_TICK_Now() - deadline) >= 0 survives the
 * 49-day rollover.
 */

#ifndef TIMER2_H
#define TIMER2_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TIMER2_SUCCESS   0
#define TIMER2_ERROR   -300

extern volatile uint32_t SYS_TICK;

extern int32_t TIMER2_Init(void);
extern uint32_t SYS_TICK_Now(void);
extern void TIMER2_ISR_Disable(void);
extern void TIMER2_ISR_Enable(void);

#ifdef __cplusplus
}
#endif

#endif /* TIMER2_H */
