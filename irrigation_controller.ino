/*
 * Smart Irrigation - Arduino Uno R3
 * Soil A0..A2, relay D7, DHT11 D6, BLE D2/D3. Pump on 6V pack via relay COM/NO.
 * Telemetry: DATA,<soil>,<rain>,<pump>,<fault>,<count>,<temp>,<hum>,<probeMap>,<waterLeft>
 * Commands: PUMP:ON|OFF, AUTO:ON|OFF, RAIN:ON|OFF, THRESH:low,high, WATER:secs|?, STATUS
 * Needs: Adafruit "DHT sensor library" + "Adafruit Unified Sensor".
 * Logic lives in .c modules; this file owns Serial/SoftwareSerial/DHT.
 */

#include <SoftwareSerial.h>
#include <DHT.h>

#include "global.h"

#if TEST_BLE_LOOPBACK
#include "test/ble_loopback_test.h"   /* local-only, gitignored */
#endif

/* C++ peripheral objects, only used in this file */
SoftwareSerial ble(PIN_BLE_RX, PIN_BLE_TX);
DHT dht(PIN_DHT, DHT11);

/* Bridge to the C++ objects (C linkage for the .c modules) */
extern "C" {

void periph_begin(void) {
  Serial.begin(9600);
  ble.begin(9600);
  dht.begin();
}

int ble_available(void) {
  return ble.available();
}

int ble_read(void) {
  return ble.read();
}

void ble_write_line(const char *text) {
  if (text != NULL) ble.println(text);
}

void ble_write_raw(const char *text) {
  if (text != NULL) ble.print(text);   /* no ending added (DEBUG_HC05_PROBE) */
}

void debug_write_line(const char *text) {
  if (text != NULL) Serial.println(text);
}

bool dht_read(float *temp, float *hum) {
  float humidity, temperature;
  if (temp == NULL || hum == NULL) return false;
  humidity = dht.readHumidity();
  temperature = dht.readTemperature();
  if (isnan(humidity) || isnan(temperature)) return false;   /* keep old values */
  *hum  = humidity;
  *temp = temperature;
  return true;
}

} /* extern "C" */

void setup() {
  sys_init();
}

void loop() {
#if TEST_BLE_LOOPBACK
  runBleLoopbackTest(ble);
#else
  unsigned long now = millis();

  handleBleCommands();

  if (now - lastDht >= DHT_INTERVAL_MS) {
    lastDht = now;
    readDht();
  }

  if (now - lastSense >= SENSE_INTERVAL_MS) {
    lastSense = now;
    readSoil();
    if (autoMode) runControlLogic();
  }

  enforcePumpSafety();

  if (now - lastReport >= REPORT_INTERVAL_MS) {
    lastReport = now;
    sendTelemetry();
  }
#endif
}
