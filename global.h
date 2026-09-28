/*
 * global.h - the single include for every .c file, and the place module
 * headers are registered. Order matters: gpio.h defines the pin levels that
 * default_config.h uses, and sys.h defines SYS_ERROR_CHECK.
 *
 * This firmware never calls init(), so the Arduino core is not part of the
 * build. pinMode(), digitalWrite(), analogRead(), millis() and delay() do not
 * exist. Everything goes through the drivers declared below.
 */

#ifndef GLOBAL_H
#define GLOBAL_H

#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The Arduino builder passes this, but a bare avr-gcc invocation may not. */
#ifndef F_CPU
#define F_CPU 16000000UL
#endif

/* swuart.c and dht11.c derive their baud and microsecond timing from F_CPU.
   Both would silently run at the wrong rate on another clock, so refuse to
   build rather than fail mysteriously on the wire. */
#if F_CPU != 16000000L
#error "This firmware is timed for a 16 MHz ATmega328P (Uno)."
#endif

/* Pin and level vocabulary, needed by default_config.h. */
#include "gpio.h"

/* Pins, calibration, thresholds, timing, debug flags. */
#include "default_config.h"

/* Error codes, SYS_ERROR_CHECK, SYS_Init. */
#include "sys.h"

/* Shared mutable state. */
#include "app_state.h"

/* Drivers. */
#include "adc.h"
#include "dht11.h"
#include "swuart.h"
#include "timer0.h"
#include "timer2.h"
#include "uart.h"

/* Feature modules. */
#include "ble_comms.h"
#include "climate_sensor.h"
#include "pump_control.h"
#include "soil_sensor.h"
#include "telemetry.h"

#endif /* GLOBAL_H */
