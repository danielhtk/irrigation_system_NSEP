/* sys.c - board bring-up. C++ objects stay in the .ino. */

#include "global.h"

#include <stdio.h>    /* snprintf */

void sys_init(void) {
  uint8_t probe;

  /* Relay OFF first: preload latch before pinMode so an
     active-low relay never blips on. Must run before anything blocking. */
#if RELAY_ACTIVE_LOW
  digitalWrite(PIN_RELAY, HIGH);        /* OFF for active-low module */
#else
  digitalWrite(PIN_RELAY, LOW);         /* OFF for active-high module */
#endif
  pinMode(PIN_RELAY, OUTPUT);
  setPump(false);                       /* sync state + re-assert OFF */

  periph_begin();
  sys_tick_begin();   /* 1 ms tick for the watering timer (Timer2) */

#if DEBUG_HC05_PROBE
  sys_debug_hc05_mac();
#endif

  for (probe = 0; probe < SOIL_SENSOR_COUNT; probe++) {
    pinMode(soil_pin(probe), INPUT);
  }

  debug_write_line("Smart Irrigation Node ready");
  ble_write_line("READY,Smart Irrigation Node");

  rxLen = 0;
  rxBuffer[0] = '\0';

  /* Drop boot chatter so it is not parsed as commands. */
  while (ble_available() > 0) {
    (void)ble_read();
  }
}

/* DEBUG_HC05_PROBE (config.h): AT console at boot.
 * Prints each reply to USB serial: AT, VERSION, LADDR/MAC, NAME, PIN, TYPE.
 * Phone must be disconnected or the module stays in data mode.
 * JDY-style boards answer with no EN jumper; genuine HC-05 needs EN to 3.3V.
 * Set the flag back to 0 when done. */
#if DEBUG_HC05_PROBE

bool sys_ble_read_line(char *line, size_t capacity, unsigned long timeout_ms) {
  unsigned long start = millis();
  size_t lineLen = 0;

  if (line == NULL || capacity == 0) {
    return false;
  }
  line[0] = '\0';

  while (millis() - start < timeout_ms) {
    while (ble_available() > 0) {
      int ch = ble_read();
      if (ch < 0) {
        break;
      }
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
  unsigned long start;

  snprintf(logLine, sizeof(logLine), "[TEMP-DEBUG] >> %s", label);
  logLine[sizeof(logLine) - 1] = '\0';
  debug_write_line(logLine);
  ble_write_raw(cmd);

  start = millis();
  while (millis() - start < 800UL) {
    if (sys_ble_read_line(replyLine, sizeof(replyLine), 200UL)) {
      snprintf(logLine, sizeof(logLine), "[HC-05] %s", replyLine);
      logLine[sizeof(logLine) - 1] = '\0';
      debug_write_line(logLine);
    }
  }
}

void sys_debug_hc05_mac(void) {
  debug_write_line("[TEMP-DEBUG] HC-05 probe start");

  /* Drain stale boot text first. */
  delay(200);
  while (ble_available() > 0) {
    (void)ble_read();
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
  while (ble_available() > 0) {
    (void)ble_read();
  }

  debug_write_line("[TEMP-DEBUG] HC-05 probe done");
}

#endif /* DEBUG_HC05_PROBE */
