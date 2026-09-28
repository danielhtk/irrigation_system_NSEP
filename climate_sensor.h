/*
 * climate_sensor.h - DHT11 polling.
 *
 * Kept old values on failure, so a bad read never looks like a change.
 */

#ifndef CLIMATE_SENSOR_H
#define CLIMATE_SENSOR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int32_t readDht(void);

#ifdef __cplusplus
}
#endif

#endif /* CLIMATE_SENSOR_H */
