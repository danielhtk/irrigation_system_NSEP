/*
 * soil_sensor.h - soil moisture + majority vote.
 *
 * Status comes back as the return value and the measurement through an
 * out-parameter, so a raw reading of 0 is never mistaken for a failure.
 */

#ifndef SOIL_SENSOR_H
#define SOIL_SENSOR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SOIL_SUCCESS             0
#define SOIL_ERROR_ARG         -700

/* SOIL_SAMPLE_COUNT lives in default_config.h (overridable via config.h). */

extern int32_t rawToPercent(int raw);
extern int32_t readSoilAveraged(uint8_t probe, int16_t *raw, int16_t *spread);
extern int32_t readSoil(void);

#ifdef __cplusplus
}
#endif

#endif /* SOIL_SENSOR_H */
