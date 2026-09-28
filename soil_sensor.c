/* soil_sensor.c - soil moisture + majority vote. No delay() here. */

#include "global.h"

#include <stdio.h>   /* snprintf for the per-probe alert codes */

int32_t rawToPercent(int raw) {
  /* Capacitive: HIGH = dry. */
  long pct = ((long)SOIL_RAW_AIR - (long)raw) * 100L
           / ((long)SOIL_RAW_AIR - (long)SOIL_RAW_WATER);
  if (pct < 0)   pct = 0;
  if (pct > 100) pct = 100;
  return (int)pct;
}

int32_t readSoilAveraged(uint8_t probe, int16_t *raw, int16_t *spread) {
  int32_t sum = 0;
  int16_t reading, lowest = 1023, highest = -1;
  uint8_t sample, channel;
  int32_t result;

  if (raw == NULL || spread == NULL) {
    return SOIL_ERROR_ARG;
  }
  if (probe >= SOIL_SENSOR_COUNT) {
    return SOIL_ERROR_ARG;
  }

  /* Channel follows config pin (A0 = ch 0). */
  if (soil_pin(probe) < 14) {
    return SOIL_ERROR_ARG;
  }
  channel = (uint8_t)(soil_pin(probe) - 14);
  if (channel > ADC_CHANNEL_MAX) {
    return SOIL_ERROR_ARG;
  }

  for (sample = 0; sample < SOIL_SAMPLE_COUNT; sample++) {
    result = ADC_Read(channel, &reading);
    if (result != ADC_SUCCESS) {
      return result;
    }
    sum += reading;
    if (reading < lowest)  { lowest = reading; }
    if (reading > highest) { highest = reading; }
  }

  *raw    = (int16_t)(sum / SOIL_SAMPLE_COUNT);
  *spread = (int16_t)(highest - lowest);

  return SOIL_SUCCESS;
}

/* Repeating alert while held. */
static void faultAlert(uint32_t *lastMs, bool *active, bool held, const char *code) {
  uint32_t now = SYS_TICK_Now();

  if (held) {
    if (!*active || (int32_t)(now - *lastMs) >= (int32_t)LEVEL_ALERT_INTERVAL_MS) {
      *active = true;
      *lastMs = now;
      sendAlert(code);
    }
  } else {
    *active = false;
  }
}

int32_t readSoil(void) {
  int32_t percent[SOIL_SENSOR_COUNT];
  bool    valid[SOIL_SENSOR_COUNT];
  uint8_t order[SOIL_SENSOR_COUNT];
  uint8_t probe, outer, inner, validCount;
  uint8_t excluded = 0;
  /* probeMap is 8 bits; more probes would silently truncate the mask. */
  _Static_assert(SOIL_SENSOR_COUNT >= 1 && SOIL_SENSOR_COUNT <= 8,
                 "SOIL_SENSOR_COUNT must be 1..8 (probeMap is a uint8_t)");
  uint8_t allBits = (uint8_t)((1u << SOIL_SENSOR_COUNT) - 1u);
  int32_t votedPct;
  int32_t result;
  int16_t raw = 0;
  int16_t spread = 0;
  static bool driftAlerted = false;   /* rising edge: outlier demoted */
  static bool splitActive = false;
  static uint32_t lastSplitMs = 0;
  static bool noQuorumActive = false;
  static uint32_t lastNoQuorumMs = 0;
  static uint32_t lastFailMs[SOIL_SENSOR_COUNT] = { 0 };
  static bool failActive[SOIL_SENSOR_COUNT] = { false };

  activeCount = 0;

  for (probe = 0; probe < SOIL_SENSOR_COUNT; probe++) {
    valid[probe] = false;
    percent[probe] = 0;

    result = readSoilAveraged(probe, &raw, &spread);
    if (result != SOIL_SUCCESS) {
      continue;               /* ADC failure: the probe simply does not vote */
    }

    percent[probe] = (int32_t)rawToPercent((int)raw);
    valid[probe]   = (raw >= SOIL_RAW_MIN && raw <= SOIL_RAW_MAX &&
                      spread <= SOIL_SPREAD_MAX);
    if (valid[probe]) {
      activeCount++;
    } else {
      excluded |= (uint8_t)(1u << probe);
    }
  }

  /* Name each failing probe, throttled like level alerts. */
  for (probe = 0; probe < SOIL_SENSOR_COUNT; probe++) {
    if (!valid[probe]) {
      char code[16];
      snprintf(code, sizeof(code), "PROBE_FAIL:%u", probe);
      code[sizeof(code) - 1] = '\0';
      faultAlert(&lastFailMs[probe], &failActive[probe], true, code);
    } else {
      faultAlert(&lastFailMs[probe], &failActive[probe], false, NULL);
    }
  }

  /* Need 2+ good probes. Otherwise keep the last trusted value. */
  if (activeCount < 2) {
    sensorFault = true;
    probeMap = allBits;
    faultAlert(&lastNoQuorumMs, &noQuorumActive, true, "SENSOR_FAULT");
    faultAlert(&lastSplitMs, &splitActive, false, NULL);
    driftAlerted = false;
    return SOIL_SUCCESS;
  }
  faultAlert(&lastNoQuorumMs, &noQuorumActive, false, NULL);

  /* Sort valid probe indices by value so outliers keep their identity. */
  validCount = 0;
  for (probe = 0; probe < SOIL_SENSOR_COUNT; probe++) {
    if (valid[probe]) order[validCount++] = probe;
  }
  for (outer = 0; outer + 1 < validCount; outer++) {
    for (inner = (uint8_t)(outer + 1); inner < validCount; inner++) {
      if (percent[order[inner]] < percent[order[outer]]) {
        uint8_t swap = order[outer];
        order[outer] = order[inner];
        order[inner] = swap;
      }
    }
  }

  /* Largest subset fitting in SOIL_SPLIT_MAX (sorted, so a sliding window).
     It must hold a strict majority of valid probes, else no side wins. */
  {
    uint8_t winStart = 0, winEnd = 1, winSize, pos;
    for (outer = 0; outer < validCount; outer++) {
      for (inner = (uint8_t)(outer + 1); inner < validCount; inner++) {
        if (percent[order[inner]] - percent[order[outer]] > SOIL_SPLIT_MAX) break;
        if ((uint8_t)(inner - outer + 1) > (uint8_t)(winEnd - winStart)) {
          winStart = outer;
          winEnd = (uint8_t)(inner + 1);
        }
      }
    }
    winSize = (uint8_t)(winEnd - winStart);
    if (winSize >= 2 && (uint16_t)(winSize * 2) > (uint16_t)validCount) {
      /* Vote = median of the agreeing set (mean of middle two if even). */
      if (winSize & 1) {
        votedPct = percent[order[winStart + winSize / 2]];
      } else {
        votedPct = (percent[order[winStart + winSize / 2 - 1]] +
                    percent[order[winStart + winSize / 2]]) / 2;
      }
      faultAlert(&lastSplitMs, &splitActive, false, NULL);
      /* Demote the rest, naming each on the rising edge only. */
      if (winSize < validCount && !driftAlerted) {
        driftAlerted = true;
        for (pos = 0; pos < validCount; pos++) {
          if (pos < winStart || pos >= winEnd) {
            char code[16];
            excluded |= (uint8_t)(1u << order[pos]);
            snprintf(code, sizeof(code), "PROBE_DRIFT:%u", order[pos]);
            code[sizeof(code) - 1] = '\0';
            sendAlert(code);
          }
        }
      } else if (winSize == validCount) {
        driftAlerted = false;
      }
    } else {
      /* No majority agrees: fault, keep last trusted value. */
      sensorFault = true;
      probeMap = allBits;
      driftAlerted = false;
      faultAlert(&lastSplitMs, &splitActive, true, "PROBE_SPLIT");
      return SOIL_SUCCESS;
    }
  }

  probeMap = excluded;
  soilPct = (int)votedPct;
  /* 0% means no data, not dry. Set SOIL_RAW_AIR = driest_real_reading + 50
     so bone-dry soil reads > 0% (e.g. 1-5%) and waters instead of faulting.
     0% only occurs on true disconnect (caught by spread) or rail short. */
  sensorFault = (votedPct < SOIL_PCT_VALID_MIN || votedPct > 100);

  return SOIL_SUCCESS;
}
