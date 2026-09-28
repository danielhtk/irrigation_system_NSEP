/*
 * sys.h - init funnel, error codes, fatal handler.
 *
 * Every driver returns int32_t: 0 on success, negative on failure.
 * SYS_ERROR_CHECK wraps a call that returns a *status*. Never wrap one that
 * returns a *measurement* (ADC_Read, GPIO_Read) - a sample of 0 is a legal
 * reading, not a failure, and those use an out-parameter instead.
 */

#ifndef SYS_H
#define SYS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SYS_SUCCESS   0
#define SYS_ERROR   -200

/* Assigns the callee's status to SYS_ERROR_CODE and halts if it is negative. */
#define SYS_ERROR_CHECK(X)                              \
    do {                                                \
        SYS_ERROR_CODE = (X);                           \
        if (SYS_ERROR_CODE < 0) {                       \
            SYS_FATAL_ERROR(SYS_ERROR_CODE, __LINE__, __FILE__); \
        }                                               \
    } while (0)

extern int32_t SYS_ERROR_CODE;

extern int32_t SYS_Init(void);

/* De-energises the relay, reports over USB serial, then halts. */
extern void SYS_FATAL_ERROR(int32_t err, int32_t line, const char *file);

#ifdef __cplusplus
}
#endif

#endif /* SYS_H */
