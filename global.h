/*
 * global.h - one include for all .c files.
 * Add new modules here. C++ libraries stay in the .ino.
 */

#ifndef GLOBAL_H
#define GLOBAL_H

/* Arduino core + libc */
#include <Arduino.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "default_config.h"
#include "app_state.h"
#include "sys.h"

/* Feature modules */
#include "sys_tick.h"
#include "soil_sensor.h"
#include "climate_sensor.h"
#include "pump_control.h"
#include "telemetry.h"
#include "ble_comms.h"

#endif /* GLOBAL_H */
