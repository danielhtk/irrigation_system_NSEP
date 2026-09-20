/*
 * sys.h - board bring-up + bridge to the C++ objects in the .ino.
 * .c files never touch Serial/SoftwareSerial/DHT directly.
 */

#ifndef SYS_H
#define SYS_H

#include <stdbool.h>
#include <stddef.h>   /* size_t */

/* Fallback when included without config.h. */
#ifndef DEBUG_HC05_PROBE
#define DEBUG_HC05_PROBE 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Board bring-up */
void sys_init(void);

/* Serial / BLE / DHT begin (in the .ino) */
void periph_begin(void);

/* BLE serial */
int  ble_available(void);
int  ble_read(void);
void ble_write_line(const char *text);
void ble_write_raw(const char *text);     /* no ending added */

/* USB serial */
void debug_write_line(const char *text);

/* DHT11. Returns false on failure, keeps old values. */
bool dht_read(float *temp, float *hum);

/* Debug probe, only when DEBUG_HC05_PROBE is 1 (see sys.c). */
#if DEBUG_HC05_PROBE

/* One LF-terminated line within timeout_ms. False on timeout. */
bool sys_ble_read_line(char *line, size_t capacity, unsigned long timeout_ms);

/* Run the AT console, print replies to USB serial. */
void sys_debug_hc05_mac(void);

#endif /* DEBUG_HC05_PROBE */

#ifdef __cplusplus
}
#endif

#endif /* SYS_H */
