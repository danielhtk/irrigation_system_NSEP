/* pump_control.c - hysteresis + safety + rain. Times use signed SYS_TICK diffs. */

#include "global.h"

#include <math.h>    /* isnan */

/* Backdated so first boot run is not blocked. */
static uint32_t lastPumpOffMs = 0UL - PUMP_MIN_OFF_MS;

/* Armed only when auto is forced off; any manual command disarms. */
static bool faultRecoveryArmed = false;
static uint32_t healthyReadings = 0;
static uint32_t lastSenseMarker = 0;

/* Level alert timers */
static uint32_t lastDroughtAlertMs = 0UL - LEVEL_ALERT_INTERVAL_MS;
static uint32_t lastFloodAlertMs = 0UL - LEVEL_ALERT_INTERVAL_MS;

int32_t setPump(bool on) {
  if (on && !pumpState) {
    pumpStartMs = SYS_TICK_Now();
  }
  if (!on && pumpState) {
    /* Stamp once: refreshing while off would block min-off forever. */
    waterDeadline = 0;            /* any stop cancels a timed run */
    lastPumpOffMs = SYS_TICK_Now(); /* track for min-off window */
  } else if (!on) {
    waterDeadline = 0;            /* already off: still cancel a stale deadline */
  }
  pumpState = on;

  return GPIO_Write(PIN_RELAY, on ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);
}

int32_t enforcePumpSafety(void) {
  uint32_t now = SYS_TICK_Now();

  /* No trusted data: drop to manual, stop auto pump. */
  bool sensorsHealthy = !sensorFault && activeCount >= 2 &&
                        soilPct >= SOIL_PCT_VALID_MIN && soilPct <= 100;

  if (!sensorsHealthy) {
    if (autoMode) {
      autoMode = false;
      faultRecoveryArmed = true;   /* arm only on the forcing transition */
      if (pumpState) {
        SYS_ERROR_CHECK(setPump(false));
      }
      sendAlert("AUTO_OFF_FAULT");
    }
    healthyReadings = 0;  /* reset on any fault */
  } else {
    /* Count healthy SENSE cycles for auto-resume. */
    if (faultRecoveryArmed && AUTO_RESUME_AFTER_FAULT) {
      if (lastSense != lastSenseMarker) {
        lastSenseMarker = lastSense;
        healthyReadings++;
        if (healthyReadings >= AUTO_RESUME_HEALTHY_READINGS) {
          autoMode = true;
          faultRecoveryArmed = false;
          sendAlert("AUTO_RESUME");
        }
      }
    } else {
      healthyReadings = 0;
      /* Reset marker when not in fault-recovery mode so we don't count stale cycles */
      lastSenseMarker = lastSense;
    }
  }

  /* 5-min cap applies in every mode. */
  if (pumpState && (int32_t)(now - pumpStartMs) >= (int32_t)PUMP_MAX_RUN_MS) {
    SYS_ERROR_CHECK(setPump(false));
    cooldownUntil = now + PUMP_COOLDOWN_MS;
    sendAlert("SAFETY_CUTOFF");
    return SYS_SUCCESS;
  }

  /* Timed run hit its deadline. */
  if (waterDeadline != 0 && (int32_t)(now - waterDeadline) >= 0) {
    SYS_ERROR_CHECK(setPump(false));   /* clears the deadline */
    sendAlert("WATER_DONE");
  }

  return SYS_SUCCESS;
}

/* Manual command cancels pending auto-resume. */
void pump_disarmAutoResume(void) {
  faultRecoveryArmed = false;
  healthyReadings = 0;
}

int32_t startTimedWater(uint32_t secs) {
  uint32_t cap = PUMP_MAX_RUN_MS / 1000UL;

  if (secs < 1) {
    secs = 1;
  }
  if (secs > cap) {
    secs = cap;
  }
  autoMode = false;
  pump_disarmAutoResume();   /* timed run is a manual command */
  SYS_ERROR_CHECK(setPump(true));
  waterDeadline = SYS_TICK_Now() + (secs * 1000UL);

  return SYS_SUCCESS;
}

uint16_t timedWaterLeft(void) {
  int32_t left;

  if (waterDeadline == 0 || !pumpState) {
    return 0;
  }
  left = (int32_t)(SYS_TICK_Now() - waterDeadline);
  if (left >= 0) {
    return 0;
  }
  return (uint16_t)((uint32_t)(-left) / 1000UL);
}

/* Rain evaluation state */
static uint32_t lastRainEvalMs = 0;
static bool lastRainSkipActive = false;
static bool lastRainHoldActive = false;
static uint32_t lastRainSkipAlertMs = 0;
static uint32_t lastRainHoldAlertMs = 0;
static uint32_t lastForecastStaleAlertMs = 0;
/* forecastReceived and lastRainfMs are in app_state.c */

/* Rain check runs every 60 s. Returns rainNow || rainAuto. */
static bool evaluateRain(void) {
  uint32_t now = SYS_TICK_Now();

  /* Rain-aware disabled? */
  if (!rainAwareEnabled) {
    rainAuto = false;
    return rainNow;
  }

  /* Fresh if seen within TTL. */
  bool forecastFresh = false;
  if (forecastReceived &&
      (int32_t)(now - lastRainfMs) <= (int32_t)RAIN_FORECAST_TTL_MS) {
    forecastFresh = true;
  } else if (forecastReceived) {
    /* Forecast stale */
    forecastReceived = false;
    if (lastForecastStaleAlertMs == 0 ||
        (int32_t)(now - lastForecastStaleAlertMs) >= (int32_t)LEVEL_ALERT_INTERVAL_MS) {
      lastForecastStaleAlertMs = now;
      sendAlert("FORECAST_STALE");
    }
  }

  /* Set at 90%, clear at 85% or when forecast ages out. */
  if (forecastFresh && !isnan(humPct) && humPct > RAIN_HUMIDITY_OBSERVED_THRESH) {
    if (!rainAuto) {
      rainAuto = true;
      sendAlert("RAIN_OBSERVED");
    }
  } else {
    /* Hysteresis clear. */
    if (rainAuto && (isnan(humPct) || humPct < (RAIN_HUMIDITY_OBSERVED_THRESH - 5) ||
                     !forecastFresh)) {
      rainAuto = false;
    }
  }

  return rainNow || rainAuto;
}

/* Returns OFF target (threshHigh, hold cap, or 0 = skip). */
static int32_t evaluateRainWatering(bool observedRain) {
  uint32_t now = SYS_TICK_Now();
  bool humidSkip;
  bool holding;

  /* Observed rain: never water, and drop any hold/skip state. */
  if (observedRain) {
    lastRainSkipActive = false;
    lastRainHoldActive = false;
    return 0;  /* Skip watering */
  }

  /* Fresh forecast: skip if the air is already saturated, otherwise hold back
   * to cap Z. Below the emergency floor we water regardless - critically dry
   * soil outranks a forecast. */
  humidSkip = forecastReceived && !isnan(humPct) &&
              humPct > RAIN_HUMIDITY_SKIP_THRESH &&
              soilPct > RAIN_EMERGENCY_FLOOR_PCT;
  holding = forecastReceived && !humidSkip;

  /* "Dry enough, nothing pending" only when no hold is in play. Cap Z sits
   * between threshLow and threshHigh, so this shortcut must not swallow it,
   * otherwise the hold could never stop the pump. */
  if (soilPct >= threshLow && !holding) {
    if (lastRainSkipActive) {
      lastRainSkipActive = false;
    }
    if (lastRainHoldActive) {
      lastRainHoldActive = false;
    }
    return threshHigh;
  }

  if (humidSkip) {
    /* Skip watering */
    if (!lastRainSkipActive ||
        (int32_t)(now - lastRainSkipAlertMs) >= (int32_t)LEVEL_ALERT_INTERVAL_MS) {
      lastRainSkipActive = true;
      lastRainSkipAlertMs = now;
      sendAlert("WATER_SKIP");
    }
    lastRainHoldActive = false;
    return 0;
  }

  if (holding) {
    /* Water to holding cap Z */
    if (!lastRainHoldActive ||
        (int32_t)(now - lastRainHoldAlertMs) >= (int32_t)LEVEL_ALERT_INTERVAL_MS) {
      lastRainHoldActive = true;
      lastRainHoldAlertMs = now;
      sendAlert("WATER_HOLD");
    }
    lastRainSkipActive = false;
    return RAIN_HOLD_CAP_Z_PCT;
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

int32_t runControlLogic(void) {
  uint32_t now = SYS_TICK_Now();
  int32_t targetOff = threshHigh;
  bool observedRain;

  /* Any of these forces the pump off. 0% never waters. */
  if (sensorFault)              { SYS_ERROR_CHECK(setPump(false)); return SYS_SUCCESS; }
  if (activeCount < 2)          { SYS_ERROR_CHECK(setPump(false)); return SYS_SUCCESS; }
  if (soilPct < SOIL_PCT_VALID_MIN || soilPct > 100) {
    SYS_ERROR_CHECK(setPump(false));
    return SYS_SUCCESS;
  }

  /* Rain evaluation (throttled) */
  if ((int32_t)(now - lastRainEvalMs) >= (int32_t)RAIN_EVAL_INTERVAL_MS) {
    lastRainEvalMs = now;
    observedRain = evaluateRain();
  } else {
    observedRain = rainNow || rainAuto;
  }

  if (observedRain)             { SYS_ERROR_CHECK(setPump(false)); return SYS_SUCCESS; }
  if ((int32_t)(now - cooldownUntil) < 0) { SYS_ERROR_CHECK(setPump(false)); return SYS_SUCCESS; }

  /* Anti-short-cycle: minimum off time between auto runs.
   * Only gates pump START, not the whole tail (alerts still evaluated). */
  if (pumpState || (int32_t)(now - lastPumpOffMs) >= (int32_t)PUMP_MIN_OFF_MS) {
    /* Rain-awareness only picks the stop target (and may veto watering for this
     * cycle). The basic threshLow -> targetOff hysteresis runs either way, so
     * clearing the flag must not disable automatic watering altogether. */
    if (rainAwareEnabled) {
      targetOff = evaluateRainWatering(observedRain);
    }

    if (targetOff != 0) {              /* 0 = skip watering this cycle */
      if (!pumpState && soilPct < threshLow) {
        SYS_ERROR_CHECK(setPump(true));
        sendAlert("PUMP_ON_DRY");
      }
      else if (pumpState && soilPct >= targetOff) {
        /* Check min-run before stopping on threshold */
        if ((int32_t)(now - pumpStartMs) >= (int32_t)PUMP_MIN_RUN_MS) {
          SYS_ERROR_CHECK(setPump(false));
          if (rainAwareEnabled && targetOff == RAIN_HOLD_CAP_Z_PCT) {
            sendAlert("PUMP_OFF_HOLD");
          } else {
            sendAlert("PUMP_OFF_TARGET");
          }
        }
      }
    }
  }

  /* Level alerts: at most one per interval (always evaluated). */
  if (soilPct <= ALERT_DROUGHT_PCT &&
      (int32_t)(now - lastDroughtAlertMs) >= (int32_t)LEVEL_ALERT_INTERVAL_MS) {
    lastDroughtAlertMs = now;
    sendAlert("DROUGHT");
  }
  if (soilPct >= ALERT_FLOOD_PCT &&
      (int32_t)(now - lastFloodAlertMs) >= (int32_t)LEVEL_ALERT_INTERVAL_MS) {
    lastFloodAlertMs = now;
    sendAlert("OVERWATERED");
  }

  return SYS_SUCCESS;
}
