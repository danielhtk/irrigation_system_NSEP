/* telemetry.h - DATA + ALERT lines.
 * DATA,<soil>,<rain>,<pump>,<fault>,<count>,<temp>,<hum>,<probeMap>,<waterLeft>
 * probeMap bit i = probe i excluded from the vote.
 * waterLeft = seconds left on a timed run, 0 when none. */

#ifndef TELEMETRY_H
#define TELEMETRY_H

#ifdef __cplusplus
extern "C" {
#endif

void sendTelemetry(void);
void sendAlert(const char *code);

#ifdef __cplusplus
}
#endif

#endif /* TELEMETRY_H */
