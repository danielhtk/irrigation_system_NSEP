/* soil_sensor.h - soil moisture + vote */

#ifndef SOIL_SENSOR_H
#define SOIL_SENSOR_H

#include <Arduino.h>   /* uint8_t */

#ifdef __cplusplus
extern "C" {
#endif

int  rawToPercent(int raw);
int  readSoilAveraged(uint8_t pin, int *spread);   /* mean; spread = max-min, NULL ok */
void readSoil(void);

#ifdef __cplusplus
}
#endif

#endif /* SOIL_SENSOR_H */
