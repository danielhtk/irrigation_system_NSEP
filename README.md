# Smart Irrigation Controller (Arduino Uno R3)

Soil-moisture-driven pump controller with hysteresis, sensor voting,
safety cutoffs, CSV-over-BLE telemetry, and an optional local dashboard
bridge server.

## Hardware

| Part | Pin |
| ---- | --- |
| 3x capacitive soil sensor v1.2 | A0, A1, A2 |
| 1-channel 5V relay (active-low) | D7 |
| DHT11 temperature / humidity | D6 |
| BLE module (HC-05 / HC-06 / JDY-style) | D2 (Arduino RX), D3 (Arduino TX, use divider) |
| Water pump (6V pack, via relay COM/NO) | — |

Serial monitor and BLE both run at 9600 baud.

## Repo layout

- `irrigation_controller.ino` — owns `Serial` / `SoftwareSerial` / `DHT`,
  implements the bridge, `setup()`/`loop()`.
- `global.h` — single include point for all `.c` modules.
- `config.h` — pins, calibration, thresholds, timing, debug-probe flags.
- `sys.c` / `sys.h` — board bring-up, fail-safe relay init, main scheduler.
- `sys_tick.*` — 1 ms system tick from Timer2 (compare with signed diffs).
- `pump_control.*` — hysteresis + 5 min safety cutoff + cooldown, plus the
  firmware watering timer (`WATER:secs`, capped at the cutoff).
- `soil_sensor.*` — raw-to-% mapping, per-probe checks, majority vote
  (see Sensor voting below).
- `climate_sensor.*` — DHT11 polling.
- `pump_control.*` — hysteresis + 5 min safety cutoff + cooldown.
- `telemetry.*` — `DATA,...` / `ALERT,...` output.
- `ble_comms.*` — line-based command parser.
- `app_state.*` — shared mutable state.
- `irrigation_server/` — Flask bridge + browser dashboard (see its own
  `README.md`).

## Firmware setup

1. Board: `arduino:avr:uno` 
2. Install libraries: "DHT sensor library" by Adafruit + "Adafruit Unified
   Sensor". `SoftwareSerial` ships with the Arduino core.
3. Tune `config.h`: `SOIL_RAW_AIR` / `SOIL_RAW_WATER` for your probes,
   `THRESH_LOW_DEFAULT` / `THRESH_HIGH_DEFAULT`, relay polarity.
4. Build and upload `irrigation_controller.ino` from the Arduino IDE.

## Sensor voting

Probes must sit in the same zone — voting only makes sense for redundant
sensors. Every sense cycle (2 s):

1. Each probe is read 5x and averaged. It votes only if the mean is inside
   `SOIL_RAW_MIN..MAX` (shorted/unplugged to a rail) **and** the 5 samples
   stay within `SOIL_SPREAD_MAX` (a floating unplugged pin wanders).
2. Fewer than 2 voting probes → `sensorFault`, last trusted value kept,
   pump forced off.
3. Otherwise the largest subset fitting inside `SOIL_SPLIT_MAX` (default 25)
   wins, but only with a strict majority of valid probes. The vote is that
   subset's median (mean of middle two if even).
4. A voted `0%` counts as no data, never waters — so keep `SOIL_RAW_AIR`
   above the driest real reading or bone-dry soil will fault instead of
   watering.

| Situation | Pump | Alert |
|---|---|---|
| All voters agree | normal | — |
| One probe failing checks | keeps watering if 2+ still vote | `ALERT,PROBE_FAIL:<i>` naming it, repeats while failing |
| One outlier, rest agree | keeps watering on the majority | `ALERT,PROBE_DRIFT:<i>` naming the demoted probe |
| No majority agrees (e.g. 2-vs-2 split) | off (fault) | `ALERT,PROBE_SPLIT`, repeats while split |
| Fewer than 2 voting probes | off (fault) | `ALERT,SENSOR_FAULT`, repeats while faulted |

`PROBE_DRIFT` fires once per episode; the others repeat at the level-alert
interval so a dead sensor can't be missed. Tune via `SOIL_RAW_MIN` /
`SOIL_RAW_MAX`, `SOIL_SPREAD_MAX`, `SOIL_SPLIT_MAX` in `config.h`.

## Debug probes (`config.h`)

No code commenting needed — set to `1` to enable, `0` to disable
(compiled out when off):

- `DEBUG_HC05_PROBE` — AT console at boot (`AT`, `AT+VERSION`, `AT+LADDR`,
  `AT+MAC`, `AT+NAME`, `AT+PIN`, `AT+TYPE`). Phone must be disconnected.
- `DEBUG_BLE_SNIFF` — logs every raw BLE byte as `[RX]...` lines.

## Telemetry and commands

Out: `DATA,<soilPct>,<rain>,<pump>,<sensorFault>,<activeCount>,<tempC>,<humPct>,<probeMap>,<waterLeft>`
(`probeMap` bit `i` = probe `i` excluded from the vote; `waterLeft` =
seconds left on a timed run, 0 when none; both absent on old firmware)

Alerts: `ALERT,<code>` where code is `PUMP_ON_DRY`, `PUMP_OFF_TARGET`,
`PUMP_OFF_HOLD` (reached hold cap Z), `WATER_DONE` (timed run finished),
`SAFETY_CUTOFF`, `DROUGHT`, `OVERWATERED`, `PROBE_FAIL:<i>` (probe failing
checks), `PROBE_DRIFT:<i>` (probe demoted but watering continues),
`PROBE_SPLIT` (no agreeing pair, pump off), `SENSOR_FAULT` (fewer than
2 voters, pump off), `AUTO_OFF_FAULT` (kicked out of auto into manual),
`MANUAL_OVERRIDE` (pump forced on without trusted data),
`AUTO_RESUME` (auto re-enabled after fault recovery),
`WATER_SKIP` (rain forecast + humidity → skipped),
`WATER_HOLD` (rain forecast → capped at Z),
`FORECAST_STALE` (forecast older than 2 h),
`RAIN_OBSERVED` (auto-detected rain via humidity),
`RAIN_LOGIC_DISABLED` (manual `RAIN:OFF` while rain logic enabled).

In: `PUMP:ON` | `PUMP:OFF` | `AUTO:ON` | `AUTO:OFF` | `RAIN:ON` |
`RAIN:OFF` | `THRESH:low,high` | `WATER:secs` | `WATER:?` | `STATUS` | `RAINF:mm,hours`

`RAINF:mm,hours` is sent by the server (Open-Meteo forecast, hourly).
`WATER:secs` runs the pump up to `secs` (clamped to the 5-min cutoff),
any stop cancels it; `WATER:?` replies `WATER:<seconds-left>`.

Safety: without trusted data the system drops to manual mode — a running
auto pump stops at once (`AUTO_OFF_FAULT`) and `AUTO:ON` is rejected with
`ERR,SENSOR_FAULT` until sensors recover. Manual `PUMP:ON` still works as
an override (`ACK,PUMP:ON_OVERRIDE` + `MANUAL_OVERRIDE` alert): you decide
how long it runs, `PUMP_MAX_RUN_MS` (5 min) is the hard cap, then cooldown.
`WATER:secs` is the timed version — firmware stops the pump itself with
`WATER_DONE`, and the remaining seconds stream in telemetry.
Voted `0%` never triggers auto watering (treated as no data).
No-water-possible states (split, fewer than 2 voters) raise repeating
check-sensors alerts; per-probe dots in the dashboard show who is out.

## Rain-aware watering

The firmware supports rain-aware auto watering. It uses a **server-provided forecast** (Open-Meteo, polled hourly by the Python bridge) and **local DHT11 humidity** to decide whether to skip, hold, or water fully.

### Decision tree (evaluated every 60 s)

1. **Soil below `threshLow`?** No → do nothing.
2. **Observed rain now?** (`rainNow` OR `rainAuto`) → skip watering entirely.
   - `rainNow`: manual latch via `RAIN:ON` / `RAIN:OFF` commands.
   - `rainAuto`: auto-detected when humidity > 90% **and** forecast rain expected. Hysteresis: sets at 90%, clears at 85% or forecast age-out.
3. **Fresh forecast ≥ 2 mm within 12 h?** (`RAINF:<mm>,<hours>` from server, TTL 2 h)
   - **Yes** → cross-check humidity:
     - Humidity > 80% **and** soil > 15% floor → **skip** (`WATER_SKIP` alert).
     - Otherwise → **water to hold cap Z (25%)** (`WATER_HOLD` alert). NaN humidity never skips.
   - **No/light rain** → full water to `threshHigh`.
4. Pump ON trigger stays single (`threshLow`); only OFF target is dynamic (Z vs High).
5. **Anti-short-cycle**: `PUMP_MIN_RUN_MS` (10 s) + `PUMP_MIN_OFF_MS` (2 min). Safety stops (fault, rain, cutoff, deadline, `PUMP:OFF`) always override min-run.

### Config (`config.h`)

| Parameter | Default | Meaning |
|-----------|---------|---------|
| `RAIN_AWARE_ENABLED` | 1 | Master switch (config only) |
| `RAIN_FORECAST_X_HOURS` | 12 | Forecast lookahead window |
| `RAIN_FORECAST_Y_MM` | 2 | Minimum mm to care about |
| `RAIN_HOLD_CAP_Z_PCT` | 25 | Hold cap between low/high |
| `RAIN_HUMIDITY_SKIP_THRESH` | 80 | Skip if humidity > this |
| `RAIN_HUMIDITY_OBSERVED_THRESH` | 90 | Auto-detected rain set point |
| `RAIN_EMERGENCY_FLOOR_PCT` | 15 | Floor: below this → water to Z |
| `RAIN_FORECAST_TTL_MS` | 2 h | Forecast staleness timeout |
| `RAIN_EVAL_INTERVAL_MS` | 60 s | Rain logic evaluation cadence |

### New commands

| Command | Direction | Description |
|---------|-----------|-------------|
| `RAINF:<mm>,<hours>` | Server → Firmware | Forecast (e.g., `RAINF:3,10` = 3 mm within 10h). Sent hourly. |
| `RAIN:ON` / `RAIN:OFF` | Dashboard → Firmware | Manual rain latch (blocks auto). `RAIN:OFF` also clears `rainAuto`. |

**`WATER:<secs>` during a timed run:** rejected with `ERR,WATER_ACTIVE`.  
**`PUMP:ON` during a timed run:** clears the deadline and restarts pump without a deadline (manual override).  
**`PUMP:OFF` during a timed run:** stops pump and clears deadline.

### New alerts

| Alert | Meaning |
|-------|---------|
| `WATER_SKIP` | Forecast + humidity > 80% + soil > 15% → watering skipped |
| `WATER_HOLD` | Forecast but no skip → watering capped at 25% (hold cap) |
| `FORECAST_STALE` | No fresh forecast for >2 h, degraded to sensor-only logic |
| `RAIN_OBSERVED` | Auto-detected rain (humidity > 90% + forecast) |
| `RAIN_LOGIC_DISABLED` | Manual `RAIN:OFF` received while rain logic enabled |

### Auto-resume after fault

Config `AUTO_RESUME_AFTER_FAULT` (default 0 = manual). If enabled, auto resumes after `AUTO_RESUME_HEALTHY_READINGS` (default 10) consecutive **healthy sense cycles** (2 s each = 20 s). Counts sense cycles, not loop iterations.

## Dashboard bridge

```sh
pip install -r irrigation_server/requirements.txt
python irrigation_server/server.py --port COM5
# then open http://localhost:5000
```

Simulated mode (`--simulate`) and Linux BLE via `bluetoothctl` (`--ble
<MAC>`) are supported. Full details in `irrigation_server/README.md`.
