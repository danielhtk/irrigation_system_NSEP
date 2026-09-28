/* ble_comms.c - line-based BLE commands over the bit-bang UART.
 *
 * The line protocol and the reply strings are unchanged, so the Python bridge
 * needs no changes. Only the byte source moved: SWUART_Read_Byte instead of
 * SoftwareSerial::available / ::read.
 */

#include "global.h"

#include <ctype.h>    /* isspace, toupper */
#include <stdio.h>    /* snprintf */
#include <stdlib.h>   /* atol */
#include <string.h>   /* strcmp, strncmp, strchr, memmove, memcpy */

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
  char logLine[44];

  bleWriteLine(reply);
  strncpy(logLine, "BLE< ", 5);
  strncpy(logLine + 5, (reply != NULL) ? reply : "?", sizeof(logLine) - 5);
  logLine[sizeof(logLine) - 1] = '\0';
  debugWriteLine(logLine);
}

/* Trusted soil data available (auto mode needs it). */
static bool trustedData(void) {
  return !sensorFault && activeCount >= 2 &&
         soilPct >= SOIL_PCT_VALID_MIN && soilPct <= 100;
}

/* WATER:<secs> runs the pump up to <secs> (capped at the 5-min cutoff).
 * WATER:? replies with seconds left on the current run.
 * During active watering, new WATER commands are ignored (only PUMP:OFF accepted). */
static void __attribute__((noinline)) handle_water(const char *args) {
  char reply[32];
  long secs;
  uint32_t cap = PUMP_MAX_RUN_MS / 1000UL;
  bool wasRunning = pumpState;

  if (args != NULL && strcmp(args, "?") == 0) {
    snprintf(reply, sizeof(reply), "WATER:%u", (unsigned)timedWaterLeft());
    reply[sizeof(reply) - 1] = '\0';
    ble_reply(reply);
    return;
  }
  if (args == NULL || args[0] == '\0') {
    ble_reply("ERR,WATER_FORMAT");
    return;
  }
  /* Non-numeric input is a format error, not a 1-second run (atol would
   * silently return 0 and the clamp below would start the pump). */
  {
    const char *digit;
    for (digit = args; *digit != '\0'; digit++) {
      if (!isdigit((unsigned char)*digit)) {
        ble_reply("ERR,WATER_FORMAT");
        return;
      }
    }
  }
  /* Ignore new WATER command if pump is already running a timed water */
  if (pumpState && waterDeadline != 0) {
    ble_reply("ERR,WATER_ACTIVE");
    return;
  }

  secs = atol(args);
  if (secs < 1) {
    secs = 1;
  }
  if ((uint32_t)secs > cap) {
    secs = (long)cap;
  }
  autoMode = false;
  SYS_ERROR_CHECK(startTimedWater((uint32_t)secs));
  if (trustedData()) {
    snprintf(reply, sizeof(reply), "ACK,WATER:%ld", secs);
  } else {
    /* No trusted data: user's call, capped like any timed run. */
    if (!wasRunning) {
      sendAlert("MANUAL_OVERRIDE");
    }
    snprintf(reply, sizeof(reply), "ACK,WATER:%ld_OVERRIDE", secs);
  }
  reply[sizeof(reply) - 1] = '\0';
  ble_reply(reply);
}

/* Parse "low,high" from THRESH:low,high. */
static void __attribute__((noinline)) handle_thresh(const char *args) {
  const char *comma;
  char  lowText[8], highText[8], reply[40];
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
static void __attribute__((noinline)) handle_rainf(const char *args) {
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
  lastRainfMs = SYS_TICK_Now();

  ble_reply("ACK,RAINF");
}

#if DEBUG_SENSOR_DIAG
/* DIAG: raw sensor state, for bringing up hardware.
 *
 * DHT bus level is sampled with the pull-up enabled before the read, so a low
 * reading proves the line is being held down rather than idling high. Bits is
 * the number of frame bits the driver decoded: 0 means the sensor never
 * answered, 40 means it answered but the checksum or timing failed. */
static void __attribute__((noinline)) handle_diag(void) {
  char line[44];
  float temp = 0.0f, hum = 0.0f;
  int16_t vbg = 0, ctl = 0;
  int32_t dhtRc;
  uint8_t probe, bits;
  int idle, n;

  debugWriteLine(">>> DIAG command received"); 
  SYS_ERROR_CHECK(GPIO_Mode(PIN_DHT, GPIO_INPUT_PULLUP));
  idle = GPIO_Read(PIN_DHT);

  dhtRc = DHT11_Read(&temp, &hum);
  bits = DHT11_LastBits();

  snprintf(line, sizeof(line), "DIAG,DHT rc=%ld b=%u i=%d",
           (long)dhtRc, (unsigned)bits, idle);
  line[sizeof(line) - 1] = '\0';
  ble_reply(line);

  strncpy(line, "DIAG,SOIL", 9);
  n = 9;
  for (probe = 0; probe < SOIL_SENSOR_COUNT; probe++) {
    int16_t raw = 0, spread = 0;
    int wrote;

    if (readSoilAveraged(probe, &raw, &spread) != SOIL_SUCCESS) {
      continue;
    }
    if (n + 12 > (int)sizeof(line)) {
      break;
    }
    wrote = snprintf(line + n, sizeof(line) - n, " %u=%d/%d",
                     (unsigned)probe, (int)raw, (int)spread);
    if (wrote < 0) {
      break;
    }
    n += wrote;
    if (n >= (int)sizeof(line)) {
      n = (int)sizeof(line) - 1;
      break;
    }
  }
  line[sizeof(line) - 1] = '\0';
  ble_reply(line);

  if (ADC_ReadBandgap(&vbg) != ADC_SUCCESS) {
    vbg = -1;
  }

  /* Control channel: nothing is wired to A3, so it shows what this ADC reads
   * for a floating input. If it matches the probe values, the ADC and its
   * reference are fine and the probe signal is not reaching the pin. */
  if (ADC_Read(3, &ctl) != ADC_SUCCESS) {
    ctl = -1;
  }
  snprintf(line, sizeof(line), "DIAG,VBG=%d A3=%d", (int)vbg, (int)ctl);
  line[sizeof(line) - 1] = '\0';
  ble_reply(line);
}
#endif /* DEBUG_SENSOR_DIAG */

/* DEBUG_BLE_SNIFF: log each byte as it arrives. */
#if DEBUG_BLE_SNIFF
static void temp_ble_sniff(int ch) {
  char logLine[24];

  if (ch == '\r') {
    debugWriteLine("[RX]<CR>");
  } else if (ch == '\n') {
    debugWriteLine("[RX]<LF>");
  } else if (ch >= 32 && ch <= 126) {
    snprintf(logLine, sizeof(logLine), "[RX]'%c'", ch);
    logLine[sizeof(logLine) - 1] = '\0';
    debugWriteLine(logLine);
  } else {
    snprintf(logLine, sizeof(logLine), "[RX]0x%02X", ch);
    logLine[sizeof(logLine) - 1] = '\0';
    debugWriteLine(logLine);
  }
}
#endif /* DEBUG_BLE_SNIFF */

static void __attribute__((noinline)) echo_rx_line(const char *text) {
  char echoLine[32];

  strncpy(echoLine, "BLE> ", 5);
  strncpy(echoLine + 5, text, sizeof(echoLine) - 5);
  echoLine[sizeof(echoLine) - 1] = '\0';
  debugWriteLine(echoLine);
}

void handleBleCommands(void) {
  uint8_t ch;

  while (SWUART_Read_Byte(&ch) == SWUART_SUCCESS) {
#if DEBUG_BLE_SNIFF
    temp_ble_sniff((int)ch);
#endif
    if (ch == '\n' || ch == '\r') {
      if (rxLen > 0) {
        rxBuffer[rxLen] = '\0';
        /* "BLE>" = incoming. Buffer is trimmed/uppercased next. */
        echo_rx_line(rxBuffer);
        // debugWriteLine(">>> Calling processCommand");
        processCommand(rxBuffer);
        // debugWriteLine(">>> processCommand returned");
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

int32_t processCommand(char *cmd) {
  if (cmd == NULL) {
    return SYS_SUCCESS;
  }
  cmd_trim(cmd);
  cmd_upper(cmd);
  // debugWriteLine(">>> processCommand: trimmed/uppered");

  /* Ignore AT leftovers without replying, else we ping-pong with the module. */
  if (strcmp(cmd, "OK") == 0 || strcmp(cmd, "ERROR") == 0 ||
      strncmp(cmd, "OK+", 3) == 0) {
    // debugWriteLine(">>> Ignored AT leftover");
    return SYS_SUCCESS;
  }

  if (strcmp(cmd, "PUMP:ON") == 0) {
    bool wasRunning = pumpState;

    autoMode = false;                 /* manual override */
    pump_disarmAutoResume();          /* user owns it now: no fault-resume */
    SYS_ERROR_CHECK(setPump(true));
    /* Manual override clears any timed watering deadline */
    waterDeadline = 0;
    if (trustedData()) {
      ble_reply("ACK,PUMP:ON");
    } else {
      /* No trusted data: user's call, capped by PUMP_MAX_RUN_MS. */
      if (!wasRunning) {
        sendAlert("MANUAL_OVERRIDE");
      }
      ble_reply("ACK,PUMP:ON_OVERRIDE");
    }
  }
  else if (strcmp(cmd, "PUMP:OFF") == 0) {
    autoMode = false;
    pump_disarmAutoResume();
    SYS_ERROR_CHECK(setPump(false));
    ble_reply("ACK,PUMP:OFF");
  }
  else if (strcmp(cmd, "AUTO:ON") == 0) {
    if (!trustedData()) {
      autoMode = false;   /* stay manual until sensors recover */
      ble_reply("ERR,SENSOR_FAULT");
    } else {
      autoMode = true;
      pump_disarmAutoResume();   /* accepted choice, not a fault recovery */
      ble_reply("ACK,AUTO:ON");
    }
  }
  else if (strcmp(cmd, "AUTO:OFF") == 0) {
    autoMode = false;
    pump_disarmAutoResume();
    ble_reply("ACK,AUTO:OFF");
  }
  else if (strcmp(cmd, "RAIN:ON") == 0) {
    rainNow = true;
    ble_reply("ACK,RAIN:ON");
  }
  else if (strcmp(cmd, "RAIN:OFF") == 0) {
    rainNow = false;
    /* rainAuto is sensor-observed, not a latch: it clears only through the
     * humidity hysteresis or a stale forecast (see evaluateRain). */
    if (rainAwareEnabled) {
      sendAlert("RAIN_MANUAL_OFF");   /* only the manual latch cleared */
    }
    ble_reply("ACK,RAIN:OFF");
  }
  else if (strcmp(cmd, "STATUS") == 0) {
    SYS_ERROR_CHECK(sendTelemetry());
  }
  else if (strcmp(cmd, "DIAG") == 0) {
#if DEBUG_SENSOR_DIAG
    handle_diag();
#else
    ble_reply("ERR,DIAG_DISABLED");
#endif
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

  return SYS_SUCCESS;
}

/* ------------------------------------------------------------------ *
 * DEBUG_HC05_PROBE: AT console at boot.
 * ------------------------------------------------------------------ */
#if DEBUG_HC05_PROBE

/* One LF-terminated line within timeout_ms. False on timeout. */
static bool sys_ble_read_line(char *line, size_t capacity, uint32_t timeout_ms) {
  uint32_t start = SYS_TICK_Now();
  size_t lineLen = 0;
  uint8_t ch;

  if (line == NULL || capacity == 0) {
    return false;
  }
  line[0] = '\0';

  while ((int32_t)(SYS_TICK_Now() - start) < (int32_t)timeout_ms) {
    while (SWUART_Read_Byte(&ch) == SWUART_SUCCESS) {
      if (ch == '\n') {
        if (lineLen > 0 && line[lineLen - 1] == '\r') {
          line[lineLen - 1] = '\0';
        }
        return true;
      }
      if (lineLen + 1 < capacity) {
        line[lineLen++] = (char)ch;
        line[lineLen] = '\0';
      }
      /* else: drop overflow chars until the newline */
    }
  }
  return false;   /* timeout */
}

/* Send one AT command, print replies for ~800 ms. */
static void temp_at_try(const char *label, const char *cmd) {
  char replyLine[48], logLine[64];
  uint32_t start;

  snprintf(logLine, sizeof(logLine), "[TEMP-DEBUG] >> %s", label);
  logLine[sizeof(logLine) - 1] = '\0';
  debugWriteLine(logLine);
  bleWriteRaw(cmd);

  start = SYS_TICK_Now();
  while ((int32_t)(SYS_TICK_Now() - start) < 800) {
    if (sys_ble_read_line(replyLine, sizeof(replyLine), 200)) {
      snprintf(logLine, sizeof(logLine), "[HC-05] %s", replyLine);
      logLine[sizeof(logLine) - 1] = '\0';
      debugWriteLine(logLine);
    }
  }
}

void sysDebugHc05Mac(void) {
  debugWriteLine("[TEMP-DEBUG] HC-05 probe start");

  /* Drain stale boot text first. */
  {
    uint8_t discard;
    uint32_t start = SYS_TICK_Now();
    while ((int32_t)(SYS_TICK_Now() - start) < 200) {
      while (SWUART_Read_Byte(&discard) == SWUART_SUCCESS) {
        /* discard */
      }
    }
  }
  {
    uint8_t discard;
    while (SWUART_Read_Byte(&discard) == SWUART_SUCCESS) {
      /* discard */
    }
  }

  /* JDY-style firmware wants CR/LF and answers +X=... */
  temp_at_try("AT", "AT\r\n");
  temp_at_try("AT+VERSION", "AT+VERSION\r\n");
  temp_at_try("AT+LADDR (MAC?)", "AT+LADDR\r\n");
  temp_at_try("AT+MAC (MAC?)", "AT+MAC\r\n");
  temp_at_try("AT+NAME (broadcast name?)", "AT+NAME\r\n");
  temp_at_try("AT+PIN (pairing password?)", "AT+PIN\r\n");
  temp_at_try("AT+TYPE (password mode?)", "AT+TYPE\r\n");

  /* Drain leftovers so they are not parsed as commands. */
  {
    uint8_t discard;
    while (SWUART_Read_Byte(&discard) == SWUART_SUCCESS) {
      /* discard */
    }
  }

  debugWriteLine("[TEMP-DEBUG] HC-05 probe done");
}

#endif /* DEBUG_HC05_PROBE */
