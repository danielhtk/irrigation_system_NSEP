/*
 * ble_comms.h - command parser for the BLE link.
 *
 * PUMP:ON|OFF, AUTO:ON|OFF, RAIN:ON|OFF, THRESH:low,high, WATER:secs|?,
 * STATUS, RAINF:mm,hours
 */

#ifndef BLE_COMMS_H
#define BLE_COMMS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Drain the BLE receive ring and dispatch every complete line. */
extern void handleBleCommands(void);
extern int32_t processCommand(char *cmd);   /* trims/uppercases cmd in place */

#if DEBUG_HC05_PROBE
/* AT console at boot. The phone must be disconnected or the module stays in
 * data mode. JDY-style boards answer with no EN jumper; a genuine HC-05 needs
 * EN tied to 3.3V. Set DEBUG_HC05_PROBE back to 0 when done. */
extern void sysDebugHc05Mac(void);
#endif /* DEBUG_HC05_PROBE */

#ifdef __cplusplus
}
#endif

#endif /* BLE_COMMS_H */
