/*
 * adc.h - register-level ADC driver, channels A0..A5.
 *
 * ADC_Read reports status through its return value and the sample through an
 * out-parameter. Never wrap a data value in SYS_ERROR_CHECK: the point of the
 * split is that a sample of 0 is a legal reading, not a failure.
 */

#ifndef ADC_H
#define ADC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ADC_SUCCESS           0
#define ADC_ERROR_CHANNEL   -400
#define ADC_ERROR_ARG       -401

#define ADC_CHANNEL_MAX        5   /* A0..A5 on the Uno */

extern int32_t ADC_Init(void);
extern int32_t ADC_Read(uint8_t channel, int16_t *result);
extern int32_t ADC_ReadBandgap(int16_t *result);

#ifdef __cplusplus
}
#endif

#endif /* ADC_H */
