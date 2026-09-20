/* pump_control.c - hysteresis + safety + rain-aware + anti-short-cycle.
 * Polarity from RELAY_ACTIVE_LOW. */

#include "global.h"
#include <math.h>    /* isnan */

/* Anti-short-cycle: track last pump-off time.
 * Init to -PUMP_MIN_OFF_MS so first run after boot isn't blocked. */
static unsigned long lastPumpOffMs = 0UL - PUMP_MIN_OFF_MS;

/* Auto-resume after fault state */
static unsigned long healthyReadings = 0;
static unsigned long lastSenseMarker = 0;

/* Level alert timers */
static unsigned long lastDroughtAlertMs = 0UL - LEVEL_ALERT_INTERVAL_MS;
static unsigned long lastFloodAlertMs = 0UL - LEVEL_ALERT_INTERVAL_MS;

void setPump(bool on) {
  if (on && !pumpState) {
    pumpStartMs = millis();
  }
  if (!on) {
    waterDeadline = 0;   /* any stop cancels a timed run */
    lastPumpOffMs = millis();  /* track for min-off window */
  }
  pumpState = on;
#if RELAY_ACTIVE_LOW
  digitalWrite(PIN_RELAY, on ? LOW : HIGH);
#else
  digitalWrite(PIN_RELAY, on ? HIGH : LOW);
#endif
}

void enforcePumpSafety(void) {
  unsigned long now = millis();

  /* Untrusted data: auto mode is unavailable. A pump left running from
     auto dies now and control drops to manual; manual overrides below
     are the user's call, capped by time. */
  bool sensorsHealthy = !sensorFault && activeCount >= 2 &&
                        soilPct >= SOIL_PCT_VALID_MIN && soilPct <= 100;

  if (!sensorsHealthy) {
    if (autoMode) {
      autoMode = false;
      if (pumpState) setPump(false);
      sendAlert("AUTO_OFF_FAULT");
    }
    healthyReadings = 0;  /* reset on any fault */
  } else {
    /* Auto-resume after fault: count consecutive healthy SENSE cycles (not loops).
     * Only increment when lastSense advances (new sense cycle completed). */
    if (!autoMode && AUTO_RESUME_AFTER_FAULT) {
      extern unsigned long lastSense;
      if (lastSense != lastSenseMarker) {
        lastSenseMarker = lastSense;
        healthyReadings++;
        if (healthyReadings >= AUTO_RESUME_HEALTHY_READINGS) {
          autoMode = true;
          sendAlert("AUTO_RESUME");
        }
      }
    } else {
      healthyReadings = 0;
      /* Reset marker when not in fault-recovery mode so we don't count stale cycles */
      extern unsigned long lastSense;
      lastSenseMarker = lastSense;
    }
  }

  /* Time cap applies in every mode: the longest any run may last. */
  if (pumpState && (now - pumpStartMs >= PUMP_MAX_RUN_MS)) {
    setPump(false);
    cooldownUntil = now + PUMP_COOLDOWN_MS;
    sendAlert("SAFETY_CUTOFF");
    return;
  }
  /* Firmware watering timer: stops the pump at the deadline. */
  if (waterDeadline != 0 && (int32_t)(sys_tick_now() - waterDeadline) >= 0) {
    setPump(false);   /* clears the deadline */
    sendAlert("WATER_DONE");
  }
}

void startTimedWater(unsigned long secs) {
  unsigned long cap = PUMP_MAX_RUN_MS / 1000UL;
  if (secs < 1) secs = 1;
  if (secs > cap) secs = cap;
  autoMode = false;
  setPump(true);
  waterDeadline = sys_tick_now() + secs * 1000UL;
}

unsigned int timedWaterLeft(void) {
  int32_t left;
  if (waterDeadline == 0 || !pumpState) return 0;
  left = (int32_t)(sys_tick_now() - waterDeadline);
  if (left >= 0) return 0;
  return (unsigned int)((uint32_t)(-left) / 1000UL);
}

/* Rain evaluation state */
static unsigned long lastRainEvalMs = 0;
static bool lastRainSkipActive = false;
static bool lastRainHoldActive = false;
static unsigned long lastRainSkipAlertMs = 0;
static unsigned long lastRainHoldAlertMs = 0;
static unsigned long lastForecastStaleAlertMs = 0;
/* forecastReceived and lastRainfMs are in app_state.c */

/* Evaluate rain logic: called from runControlLogic at RAIN_EVAL_INTERVAL_MS.
 * Returns the effective observed rain state (rainNow || rainAuto). */
static bool evaluateRain(void) {
  unsigned long now = millis();

  /* Rain-aware disabled? */
  if (!rainAwareEnabled) {
    rainAuto = false;
    return rainNow;
  }

  /* Check forecast freshness */
  bool forecastFresh = false;
  if (forecastReceived && now - lastRainfMs <= RAIN_FORECAST_TTL_MS) {
    forecastFresh = true;
  } else if (forecastReceived) {
    /* Forecast stale */
    forecastReceived = false;
    if (!lastForecastStaleAlertMs ||
        now - lastForecastStaleAlertMs >= LEVEL_ALERT_INTERVAL_MS) {
      lastForecastStaleAlertMs = now;
      sendAlert("FORECAST_STALE");
    }
  }

  /* Humidity rule for rainAuto (observed rain auto-detected) */
  if (forecastFresh && !isnan(humPct) && humPct > RAIN_HUMIDITY_OBSERVED_THRESH) {
    if (!rainAuto) {
      rainAuto = true;
      sendAlert("RAIN_OBSERVED");
    }
  } else {
    /* Clear rainAuto with hysteresis (90% set, 85% clear) or forecast aged out */
    if (rainAuto && (isnan(humPct) || humPct < (RAIN_HUMIDITY_OBSERVED_THRESH - 5) || !forecastFresh)) {
      rainAuto = false;
    }
  }

  return rainNow || rainAuto;
}

/* Evaluate rain-aware watering decision.
 * Returns target off percentage (threshHigh, RAIN_HOLD_CAP_Z_PCT, or 0 for skip).
 * 0 means skip watering entirely. */
static int evaluateRainWatering(bool observedRain) {
  unsigned long now = millis();

  /* Step 1: Soil below threshLow? */
  if (soilPct >= threshLow) {
    /* Not dry enough - clear any active rain alerts */
    if (lastRainSkipActive) {
      lastRainSkipActive = false;
    }
    if (lastRainHoldActive) {
      lastRainHoldActive = false;
    }
    return threshHigh;
  }

  /* Step 2: Observed rain now? */
  if (observedRain) {
    if (lastRainSkipActive) {
      lastRainSkipActive = false;
    }
    if (lastRainHoldActive) {
      lastRainHoldActive = false;
    }
    return 0;  /* Skip watering */
  }

  /* Step 3: Fresh forecast >= Y mm within X h? */
  if (forecastReceived) {
    /* Cross-check humidity */
    if (!isnan(humPct) && humPct > RAIN_HUMIDITY_SKIP_THRESH && soilPct > RAIN_EMERGENCY_FLOOR_PCT) {
      /* Skip watering */
      if (!lastRainSkipActive ||
          now - lastRainSkipAlertMs >= LEVEL_ALERT_INTERVAL_MS) {
        lastRainSkipActive = true;
        lastRainSkipAlertMs = now;
        sendAlert("WATER_SKIP");
      }
      lastRainHoldActive = false;
      return 0;
    } else {
      /* Water to holding cap Z */
      if (!lastRainHoldActive ||
          now - lastRainHoldAlertMs >= LEVEL_ALERT_INTERVAL_MS) {
        lastRainHoldActive = true;
        lastRainHoldAlertMs = now;
        sendAlert("WATER_HOLD");
      }
      lastRainSkipActive = false;
      return RAIN_HOLD_CAP_Z_PCT;
    }
  }

  /* No/light rain: full water to threshHigh */
  if (lastRainSkipActive) {
    lastRainSkipActive = false;
  }
  if (lastRainHoldActive) {
    lastRainHoldActive = false;
  }
  return threshHigh;
}

void runControlLogic(void) {
  unsigned long now = millis();
  int targetOff = threshHigh;
  bool observedRain;

  /* Any of these forces the pump off. 0% never waters. */
  if (sensorFault)              { setPump(false); return; }
  if (activeCount < 2)          { setPump(false); return; }
  if (soilPct < SOIL_PCT_VALID_MIN || soilPct > 100) {
    setPump(false);
    return;
  }

  /* Rain evaluation (throttled) */
  if (now - lastRainEvalMs >= RAIN_EVAL_INTERVAL_MS) {
    lastRainEvalMs = now;
    observedRain = evaluateRain();
  } else {
    observedRain = rainNow || rainAuto;
  }

  if (observedRain)             { setPump(false); return; }
  if (now < cooldownUntil)      { setPump(false); return; }

  /* Anti-short-cycle: minimum off time between auto runs.
   * Only gates pump START, not the whole tail (alerts still evaluated). */
  if (!pumpState && now - lastPumpOffMs < PUMP_MIN_OFF_MS) {
    /* Still in min-off window, but don't suppress alerts */
  } else {
    /* Determine rain-aware target off percentage */
    if (rainAwareEnabled) {
      targetOff = evaluateRainWatering(observedRain);
      if (targetOff == 0) {  /* skip watering */
        /* fall through to alerts */
      } else {
        /* Normal hysteresis with dynamic targetOff */
        if (!pumpState && soilPct < threshLow) {
          setPump(true);
          sendAlert("PUMP_ON_DRY");
        }
        else if (pumpState && soilPct >= targetOff) {
          /* Check min-run before stopping on threshold */
          if (now - pumpStartMs >= PUMP_MIN_RUN_MS) {
            setPump(false);
            if (targetOff == RAIN_HOLD_CAP_Z_PCT) {
              sendAlert("PUMP_OFF_HOLD");
            } else {
              sendAlert("PUMP_OFF_TARGET");
            }
          }
        }
      }
    }
  }

  /* Level alerts: at most one per interval (always evaluated). */
  if (soilPct <= ALERT_DROUGHT_PCT &&
      now - lastDroughtAlertMs >= LEVEL_ALERT_INTERVAL_MS) {
    lastDroughtAlertMs = now;
    sendAlert("DROUGHT");
  }
  if (soilPct >= ALERT_FLOOD_PCT &&
      now - lastFloodAlertMs >= LEVEL_ALERT_INTERVAL_MS) {
    lastFloodAlertMs = now;
    sendAlert("OVERWATERED");
  }
}