/*
 * Smart Irrigation - Arduino Uno R3, register level.
 * Soil A0..A2, relay D7, DHT11 D6, BLE D2/D3, BLE STATE D4.
 * Pump on 6V pack via relay COM/NO.
 *
 * Telemetry: DATA,<soil>,<rain>,<pump>,<fault>,<count>,<temp>,<hum>,<probeMap>,<waterLeft>
 * Commands: PUMP:ON|OFF, AUTO:ON|OFF, RAIN:ON|OFF, THRESH:low,high,
 *           WATER:secs|?, STATUS, RAINF:mm,hours
 *
 * The sketch owns nothing but the scheduler. It defines main() rather than
 * setup()/loop() and never calls init(), so the Arduino core is not linked at
 * all: no pinMode(), digitalWrite(), analogRead(), millis() or delay(). Every
 * hardware touch goes through the drivers in global.h.
 */

#include "global.h"

#if TEST_BLE_LOOPBACK
#include "test/ble_loopback_test.h"   /* local-only, gitignored */
#endif

/* Cooperative scheduler. Each task gates itself on SYS_TICK; safety runs on
 * every pass because it owns the cutoff and the watering deadline.
 * Loopback mode bypasses the scheduler entirely, so these are compiled out
 * there rather than left as unused statics. */
#if !TEST_BLE_LOOPBACK

static void task_ble(void) {
  handleBleCommands();   /* drains the swuart RX ring itself */
}

static void task_climate(void) {
  uint32_t now = SYS_TICK_Now();

  if ((int32_t)(now - lastDht) >= (int32_t)DHT_INTERVAL_MS) {
    lastDht = now;
    readDht();   /* a failed read keeps the old values */
  }
}

static void task_soil(void) {
  uint32_t now = SYS_TICK_Now();

  if ((int32_t)(now - lastSense) >= (int32_t)SENSE_INTERVAL_MS) {
    lastSense = now;
    SYS_ERROR_CHECK(readSoil());
    if (autoMode) {
      SYS_ERROR_CHECK(runControlLogic());
    }
  }
}

static void task_safety(void) {
  SYS_ERROR_CHECK(enforcePumpSafety());
}

static void task_report(void) {
  uint32_t now = SYS_TICK_Now();

  if ((int32_t)(now - lastReport) >= (int32_t)REPORT_INTERVAL_MS) {
    lastReport = now;
    SYS_ERROR_CHECK(sendTelemetry());
  }
}

#endif /* !TEST_BLE_LOOPBACK */

int main(void) {
  uint8_t ch;

  SYS_ERROR_CHECK(SYS_Init());
  sei();   /* nothing above this point relies on an ISR */

  /* Stamped after init so the DHT11 power-on guard counts from a known point. */
  bootMs = SYS_TICK_Now();

#if DEBUG_HC05_PROBE
  sysDebugHc05Mac();
#endif

  rxLen = 0;
  rxBuffer[0] = '\0';

  /* Drop module boot chatter so it is not parsed as commands. */
  while (SWUART_Read_Byte(&ch) == SWUART_SUCCESS) {
    /* discard */
  }

  SYS_ERROR_CHECK(debugWriteLine("Smart Irrigation Node ready"));
  SYS_ERROR_CHECK(bleWriteLine("READY,Smart Irrigation Node"));

  for (;;) {
    UART_Process();          /* drain the USB TX ring */

#if TEST_BLE_LOOPBACK
    runBleLoopbackTest();
#else
    task_safety();           /* cutoff and watering deadline: every pass */
    task_ble();              /* commands and module boot chatter */
    task_climate();          /* DHT11, ~25 ms blocked, see dht11.c */
    task_soil();
    task_report();
#endif
  }

  return 0;
}
