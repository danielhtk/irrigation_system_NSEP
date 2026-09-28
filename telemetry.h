/*
 * telemetry.h - DATA / ALERT output, plus the thin transport helpers the rest
 * of the firmware writes through.
 *
 * The line protocol is unchanged and is still CSV, because
 * irrigation_gateway/ parses it. Only the transport underneath moved from
 * SoftwareSerial to the bit-bang UART.
 *
 * DATA,<soil>,<rain>,<pump>,<fault>,<count>,<temp>,<hum>,<probeMap>,<waterLeft>
 * probeMap bit i = probe i excluded from the vote.
 * waterLeft = seconds left on a timed run, 0 when none.
 */

#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TELEM_MSG_SIZE 64

/* Binary data frame, modelled on Arduino_IoT (3)'s DATA_TYPE_S (a union of
 * a named-field struct and a raw byte array, sent with sizeof). That project
 * fits one float per frame; this one carries a whole report in a single frame,
 * because nine 13-byte frames would be 117 B per report against 23 B here.
 *
 * Byte layout is AVR little-endian, no padding: every field is naturally
 * aligned, so a receiver that declares the same field order and reads it with
 * unpack() gets the same bytes. Decoders must not assume struct padding from
 * another compiler's rules; decode the byte array field by field.
 *
 * flags bits, matching the CSV column order:
 *   0 rain detected   1 pump state   2 sensor fault
 *
 * tempC and humPct are the same floats the CSV line prints, so NA becomes
 * NaN here. A receiver that cannot use NaN should test for it explicitly. */
#define DATA_FLAG_RAIN   0x01u
#define DATA_FLAG_PUMP   0x02u
#define DATA_FLAG_FAUL   0x04u

extern int32_t sendTelemetry(void);
extern int32_t sendAlert(const char *code);

/* Transport helpers. A line ends CR LF: the gateway's line assembler drops the
 * CR, and the BLE module's AT console wants both. */

/* Over BLE only, no terminator (used by the AT probe). */
extern int32_t bleWriteRaw(const char *text);
/* Over BLE, CR LF appended. */
extern int32_t bleWriteLine(const char *text);
/* Over BLE, and mirrored to USB serial for a live trace. */
extern int32_t bleWriteLineMirrored(const char *text);
/* Over USB serial, CR LF appended. */
extern int32_t debugWriteLine(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* TELEMETRY_H */
