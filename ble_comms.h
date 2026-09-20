/* ble_comms.h - PUMP, AUTO, RAIN, THRESH, WATER, STATUS */

#ifndef BLE_COMMS_H
#define BLE_COMMS_H

#ifdef __cplusplus
extern "C" {
#endif

void handleBleCommands(void);
void processCommand(char *cmd);   /* trims/uppercases cmd in place */

#ifdef __cplusplus
}
#endif

#endif /* BLE_COMMS_H */
