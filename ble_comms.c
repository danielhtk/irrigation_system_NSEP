/* ble_comms.c - line-based BLE commands, plain C string ops */

#include "global.h"

#include <ctype.h>    /* isspace, toupper */
#include <stdio.h>    /* snprintf */
#include <stdlib.h>   /* atoi, atol */
#include <string.h>   /* strcmp, strncmp, strchr, memmove, memcpy, strlen */

/* Trim whitespace in place. */
static void cmd_trim(char *text) {
  size_t len, start = 0;
  if (text == NULL) {
    return;
  }
  while (text[start] != '\0' && isspace((unsigned char)text[start])) {
    start++;
  }
  if (start > 0) {
    memmove(text, text + start, strlen(text + start) + 1);
  }
  len = strlen(text);
  while (len > 0 && isspace((unsigned char)text[len - 1])) {
    text[len - 1] = '\0';
    len--;
  }
}

/* Uppercase in place. */
static void cmd_upper(char *text) {
  if (text == NULL) {
    return;
  }
  for (; *text != '\0'; text++) {
    *text = (char)toupper((unsigned char)*text);
  }
}

/* Reply over BLE, mirrored to USB ("BLE<" = outgoing). */
static void ble_reply(const char *reply) {
  char logLine[40];
  ble_write_line(reply);
  snprintf(logLine, sizeof(logLine), "BLE< %s", (reply != NULL) ? reply : "?");
  logLine[sizeof(logLine) - 1] = '\0';
  debug_write_line(logLine);
}

/* Trusted soil data available (auto mode needs it). */
static bool trustedData(void) {
  return !sensorFault && activeCount >= 2 &&
         soilPct >= SOIL_PCT_VALID_MIN && soilPct <= 100;
}

/* WATER:<secs> runs the pump up to <secs> (capped at the 5-min cutoff).
 * WATER:? replies with seconds left on the current run.
 * During active watering, new WATER commands are ignored (only PUMP:OFF accepted). */
static void handle_water(const char *args) {
  char reply[32];
  if (args != NULL && strcmp(args, "?") == 0) {
    snprintf(reply, sizeof(reply), "WATER:%u", timedWaterLeft());
    reply[sizeof(reply) - 1] = '\0';
    ble_reply(reply);
    return;
  }
  if (args == NULL || args[0] == '\0') {
    ble_reply("ERR,WATER_FORMAT");
    return;
  }
  /* Ignore new WATER command if pump is already running a timed water */
  if (pumpState && waterDeadline != 0) {
    ble_reply("ERR,WATER_ACTIVE");
    return;
  }
  {
    long secs = atol(args);
    unsigned long cap = PUMP_MAX_RUN_MS / 1000UL;
    bool wasRunning = pumpState;
    if (secs < 1) secs = 1;
    if ((unsigned long)secs > cap) secs = (long)cap;
    autoMode = false;
    startTimedWater((unsigned long)secs);
    if (trustedData()) {
      snprintf(reply, sizeof(reply), "ACK,WATER:%ld", secs);
    } else {
      /* No trusted data: user's call, capped like any timed run. */
      if (!wasRunning) sendAlert("MANUAL_OVERRIDE");
      snprintf(reply, sizeof(reply), "ACK,WATER:%ld_OVERRIDE", secs);
    }
    reply[sizeof(reply) - 1] = '\0';
    ble_reply(reply);
  }
}

/* Parse "low,high" from THRESH:low,high. */
static void handle_thresh(const char *args) {
  const char *comma;
  char  lowText[8], highText[8], reply[32];
  size_t lowLen;
  int   low, high;

  if (args == NULL) {
    ble_reply("ERR,THRESH_FORMAT");
    return;
  }
  comma = strchr(args, ',');
  if (comma == NULL) {
    ble_reply("ERR,THRESH_FORMAT");
    return;
  }
  lowLen = (size_t)(comma - args);
  if (lowLen == 0 || lowLen >= sizeof(lowText)) {
    ble_reply("ERR,THRESH_FORMAT");
    return;
  }
  memcpy(lowText, args, lowLen);
  lowText[lowLen] = '\0';
  strncpy(highText, comma + 1, sizeof(highText) - 1);
  highText[sizeof(highText) - 1] = '\0';

  low = atoi(lowText);
  high = atoi(highText);
  if (low >= 0 && high <= 100 && high > low + 5) {
    threshLow  = low;
    threshHigh = high;
    snprintf(reply, sizeof(reply), "ACK,THRESH:%d,%d", low, high);
    reply[sizeof(reply) - 1] = '\0';
    ble_reply(reply);
  } else {
    ble_reply("ERR,THRESH_RANGE");
  }
}

/* RAINF:<mm>,<hours_remaining> - forecast from server. */
static void handle_rainf(const char *args) {
  const char *comma;
  char  mmText[8], hrsText[8];
  size_t mmLen;
  long  mm, hrs;

  if (args == NULL || args[0] == '\0') {
    ble_reply("ERR,RAINF_FORMAT");
    return;
  }
  comma = strchr(args, ',');
  if (comma == NULL) {
    ble_reply("ERR,RAINF_FORMAT");
    return;
  }
  mmLen = (size_t)(comma - args);
  if (mmLen == 0 || mmLen >= sizeof(mmText)) {
    ble_reply("ERR,RAINF_FORMAT");
    return;
  }
  memcpy(mmText, args, mmLen);
  mmText[mmLen] = '\0';
  strncpy(hrsText, comma + 1, sizeof(hrsText) - 1);
  hrsText[sizeof(hrsText) - 1] = '\0';

  mm  = atol(mmText);
  hrs = atol(hrsText);

  /* Ignore if below threshold or beyond horizon */
  if (mm < RAIN_FORECAST_Y_MM || hrs > RAIN_FORECAST_X_HOURS) {
    ble_reply("ACK,RAINF:IGNORED");
    return;
  }

  /* Accept forecast - update state for rain evaluation */
  forecastReceived = true;
  lastRainfMs = millis();

  ble_reply("ACK,RAINF");
}

/* DEBUG_BLE_SNIFF: log each byte as it arrives. */
#if DEBUG_BLE_SNIFF
static void temp_ble_sniff(int ch) {
  char logLine[24];
  if (ch == '\r') {
    debug_write_line("[RX]<CR>");
  } else if (ch == '\n') {
    debug_write_line("[RX]<LF>");
  } else if (ch >= 32 && ch <= 126) {
    snprintf(logLine, sizeof(logLine), "[RX]'%c'", ch);
    logLine[sizeof(logLine) - 1] = '\0';
    debug_write_line(logLine);
  } else {
    snprintf(logLine, sizeof(logLine), "[RX]0x%02X", ch);
    logLine[sizeof(logLine) - 1] = '\0';
    debug_write_line(logLine);
  }
}
#endif /* DEBUG_BLE_SNIFF */

void handleBleCommands(void) {
  while (ble_available() > 0) {
    int ch = ble_read();
    if (ch < 0) {
      break;
    }
#if DEBUG_BLE_SNIFF
    temp_ble_sniff(ch);
#endif
    if (ch == '\n' || ch == '\r') {
      if (rxLen > 0) {
        char echoLine[40];
        rxBuffer[rxLen] = '\0';
        /* "BLE>" = incoming. Buffer is trimmed/uppercased next. */
        snprintf(echoLine, sizeof(echoLine), "BLE> %s", rxBuffer);
        echoLine[sizeof(echoLine) - 1] = '\0';
        debug_write_line(echoLine);
        processCommand(rxBuffer);
        rxLen = 0;
        rxBuffer[0] = '\0';
      }
    } else {
      if (rxLen < RX_BUF_MAX) {
        rxBuffer[rxLen++] = (char)ch;
        rxBuffer[rxLen] = '\0';
      }
      /* else: drop overflow chars until end of line */
    }
  }
}

void processCommand(char *cmd) {
  if (cmd == NULL) {
    return;
  }
  cmd_trim(cmd);
  cmd_upper(cmd);

  /* Ignore AT leftovers without replying, else we ping-pong with the module. */
  if (strcmp(cmd, "OK") == 0 || strcmp(cmd, "ERROR") == 0 ||
      strncmp(cmd, "OK+", 3) == 0) {
    return;
  }

  if (strcmp(cmd, "PUMP:ON") == 0) {
    bool wasRunning = pumpState;
    autoMode = false;                 /* manual override */
    setPump(true);
    /* Manual override clears any timed watering deadline */
    waterDeadline = 0;
    if (trustedData()) {
      ble_reply("ACK,PUMP:ON");
    } else {
      /* No trusted data: user's call, capped by PUMP_MAX_RUN_MS. */
      if (!wasRunning) sendAlert("MANUAL_OVERRIDE");
      ble_reply("ACK,PUMP:ON_OVERRIDE");
    }
  }
  else if (strcmp(cmd, "PUMP:OFF") == 0) {
    autoMode = false;
    setPump(false);
    ble_reply("ACK,PUMP:OFF");
  }
  else if (strcmp(cmd, "AUTO:ON") == 0) {
    if (!trustedData()) {
      autoMode = false;   /* stay manual until sensors recover */
      ble_reply("ERR,SENSOR_FAULT");
    } else {
      autoMode = true;
      ble_reply("ACK,AUTO:ON");
    }
  }
  else if (strcmp(cmd, "AUTO:OFF") == 0) {
    autoMode = false;
    ble_reply("ACK,AUTO:OFF");
  }
  else if (strcmp(cmd, "RAIN:ON") == 0) {
    rainNow = true;
    rainDetected = true;   /* legacy alias */
    ble_reply("ACK,RAIN:ON");
  }
  else if (strcmp(cmd, "RAIN:OFF") == 0) {
    rainNow = false;
    rainAuto = false;      /* manual OFF clears auto-detected too */
    rainDetected = false;  /* legacy alias */
    if (rainAwareEnabled) sendAlert("RAIN_LOGIC_DISABLED");
    ble_reply("ACK,RAIN:OFF");
  }
  else if (strcmp(cmd, "STATUS") == 0) {
    sendTelemetry();
  }
  else if (strncmp(cmd, "THRESH:", 7) == 0) {
    handle_thresh(cmd + 7);
  }
  else if (strncmp(cmd, "WATER:", 6) == 0) {
    handle_water(cmd + 6);
  }
  else if (strncmp(cmd, "RAINF:", 6) == 0) {
    handle_rainf(cmd + 6);
  }
  else {
    ble_reply("ERR,UNKNOWN_CMD");
  }
}