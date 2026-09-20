/* sys_tick.h - 1 ms system tick from Timer2 (CTC, 16 MHz / 64 / 250).
 * Free-running and independent of millis(). Always compare with
 * signed diffs so the 49-day rollover stays safe. */

#ifndef SYS_TICK_H
#define SYS_TICK_H

#ifdef __cplusplus
extern "C" {
#endif

void sys_tick_begin(void);   /* start the 1 ms interrupt */
unsigned long sys_tick_now(void);   /* current tick, atomic read */

#ifdef __cplusplus
}
#endif

#endif /* SYS_TICK_H */
