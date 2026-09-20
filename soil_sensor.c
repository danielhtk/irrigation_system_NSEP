/* soil_sensor.c - soil moisture + majority vote */

#include "global.h"

#include <stdio.h>   /* snprintf for drift alert */

int rawToPercent(int raw) {
  /* Capacitive: HIGH = dry. Same as map+constrain to 0..100. */
  long pct = ((long)SOIL_RAW_AIR - (long)raw) * 100L
           / ((long)SOIL_RAW_AIR - (long)SOIL_RAW_WATER);
  if (pct < 0)   pct = 0;
  if (pct > 100) pct = 100;
  return (int)pct;
}

int readSoilAveraged(uint8_t pin, int *spread) {
  long sum = 0;
  int lowest = 1024, highest = -1;
  uint8_t sample;
  for (sample = 0; sample < 5; sample++) {
    int reading = analogRead(pin);
    sum += reading;
    if (reading < lowest) lowest = reading;
    if (reading > highest) highest = reading;
    delay(4);
  }
  if (spread != NULL) *spread = highest - lowest;
  return (int)(sum / 5);
}

/* Reminder-style alert: fires at once, then at most every interval while held. */
static void faultAlert(unsigned long *lastMs, bool *active, bool held, const char *code) {
  unsigned long now = millis();
  if (held) {
    if (!*active || now - *lastMs >= LEVEL_ALERT_INTERVAL_MS) {
      *active = true;
      *lastMs = now;
      sendAlert(code);
    }
  } else {
    *active = false;
  }
}

void readSoil(void) {
  int     percent[SOIL_SENSOR_COUNT];
  bool    valid[SOIL_SENSOR_COUNT];
  uint8_t order[SOIL_SENSOR_COUNT];
  uint8_t probe, outer, inner, validCount;
  uint8_t excluded = 0;
  uint8_t allBits = (uint8_t)((1u << SOIL_SENSOR_COUNT) - 1u);
  int votedPct;
  static bool driftAlerted = false;   /* rising edge: outlier demoted */
  static bool splitActive = false;
  static unsigned long lastSplitMs = 0;
  static bool noQuorumActive = false;
  static unsigned long lastNoQuorumMs = 0;
  static unsigned long lastFailMs[SOIL_SENSOR_COUNT] = { 0 };
  static bool failActive[SOIL_SENSOR_COUNT] = { false };

  activeCount = 0;

  for (probe = 0; probe < SOIL_SENSOR_COUNT; probe++) {
    int raw, spread = 0;
    raw = readSoilAveraged(soil_pin(probe), &spread);
    valid[probe]   = (raw >= SOIL_RAW_MIN && raw <= SOIL_RAW_MAX &&
                      spread <= SOIL_SPREAD_MAX);
    percent[probe] = rawToPercent(raw);
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
      snprintf(code, sizeof(code), "PROBE_FAIL:%d", probe);
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
    return;
  }
  faultAlert(&lastNoQuorumMs, &noQuorumActive, false, NULL);

  /* Sort valid probe indices by value so outliers keep their identity. */
  validCount = 0;
  for (probe = 0; probe < SOIL_SENSOR_COUNT; probe++) {
    if (valid[probe]) order[validCount++] = probe;
  }
  for (outer = 0; outer + 1 < validCount; outer++) {
    for (inner = outer + 1; inner < validCount; inner++) {
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
        if (inner - outer + 1 > winEnd - winStart) {
          winStart = outer;
          winEnd = (uint8_t)(inner + 1);
        }
      }
    }
    winSize = (uint8_t)(winEnd - winStart);
    if (winSize >= 2 && winSize * 2 > validCount) {
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
            snprintf(code, sizeof(code), "PROBE_DRIFT:%d", order[pos]);
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
      return;
    }
  }

  probeMap = excluded;
  soilPct = votedPct;
  /* 0% means no data, not dry. SOIL_RAW_AIR_MARGIN ensures bone-dry
     soil reads > 0% (e.g. 1-5%) so it waters instead of faulting.
     0% only occurs on true disconnect (caught by spread) or rail short. */
  sensorFault = (votedPct < SOIL_PCT_VALID_MIN || votedPct > 100);
}
