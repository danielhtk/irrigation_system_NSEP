# Smart Irrigation Controller (Arduino Uno R3)

Water the plants when the soil is dry, stop when it is wet. Three soil
probes vote, a DHT11 reads air temp/humidity, and a relay switches the
pump. It talks to a Linux gateway over BLE (9600 baud).

## Hardware

| Part | Pin |
| ---- | --- |
| 3x capacitive soil sensor v1.2 | A0, A1, A2 |
| 1-channel 5V relay (active-low) | D7 |
| DHT11 temp / humidity | D6 |
| BLE module (HC-05 / HC-06 / JDY) | D2 (RX), D3 (TX, use divider) |
| BLE STATE (optional) | D4 |

Serial monitor and BLE both run at 9600 baud.

## Quick start

1. Board: `arduino:avr:uno`.
2. No libraries to install. DHT11, BLE UART and timers are all in this repo.
3. Copy the settings you need into `config.h` (gitignored). Defaults live in
   `default_config.h`. Example:
   ```c
   #define SOIL_RAW_AIR  506
   #define SOIL_RAW_WATER 200
   ```
4. Build and upload `irrigation_controller.ino` from the Arduino IDE.

If the pump runs when it should be idle, flip `RELAY_ACTIVE_LOW` in
`config.h` (default 1 = common active-low module).

## How it works

No Arduino core here: the sketch defines `main()` and never calls `init()`,
so `pinMode`, `digitalWrite`, `analogRead`, `millis`, `delay` and `Serial`
do not exist. Each driver returns `int32_t` (0 = ok) and errors go through
`SYS_ERROR_CHECK`. The one clock is `SYS_TICK` (Timer2, 1 ms, wraps every
49 days). Always compare time like this:

```c
if ((int32_t)(SYS_TICK_Now() - deadline) >= 0) { /* expired */ }
```

Timers: Timer0 feeds the BLE UART (38.4 kHz), Timer2 is the 1 ms tick,
DHT11 is bit-banged. `F_CPU` must be 16 MHz.

**Sensors (every 2 s).** Each probe is read 5x and averaged. A probe votes
only if its reading looks plugged in (inside `SOIL_RAW_MIN..MAX` and steady).
With 2+ voters the majority cluster wins; otherwise the pump is forced off
and `sensorFault` is set. A voted `0%` means "no data" and never waters.

| What happened | Pump | Alert |
|---|---|---|
| Majority agrees | normal | — |
| One probe drifts off | runs on the rest | `ALERT,PROBE_DRIFT:<i>` once |
| One probe unplugged | runs if 2 still vote | `ALERT,PROBE_FAIL:<i>` repeats |
| No majority / <2 voters | off | `ALERT,PROBE_SPLIT` or `SENSOR_FAULT` repeats |

**Pump.** Starts below `threshLow` (default 30%), stops at `threshHigh`
(default 70%). Hard cap: 5 min on (`PUMP_MAX_RUN_MS`), then 1 min cooldown.
Short-cycle guard: min 10 s on, 2 min off between auto runs.

**Rain (checked every 60 s).** Forecast comes from the gateway as
`RAINF:<mm>,<hours>` (Open-Meteo, 2 h TTL).

- Rain latched (`RAIN:ON`) or auto-detected (humidity > 90% + forecast) → skip.
- Forecast >= 2 mm in 12 h + humidity > 80% + soil > 15% → skip (`WATER_SKIP`).
- Forecast >= 2 mm but no skip → water only up to 25% cap (`WATER_HOLD`).
- Below 15% (`RAIN_EMERGENCY_FLOOR_PCT`) → ignore forecast, water anyway.
- Set `RAIN_AWARE_ENABLED` to 0 to keep plain low/high watering.

**Auto-resume.** Default on (`AUTO_RESUME_AFTER_FAULT = 1`): after a fault,
5 healthy reads in a row (10 s) puts auto mode back on with `ALERT,AUTO_RESUME`.

## Telemetry and commands

Out (every 10 s or on change):

```
DATA,<soil>,<rain>,<pump>,<fault>,<count>,<temp>,<hum>,<map>,<waterLeft>,<auto>
```

Example: `DATA,42,0,1,0,3,25.7,45.0,0,0,1` = 42% soil, pump on, 3 voters,
25.7 C, 45% humidity, auto mode. `map` marks excluded probes, `waterLeft`
is seconds left on a `WATER:secs` run.

Alerts look like `ALERT,SAFETY_CUTOFF` or `ALERT,PROBE_FAIL:1`.
Full list is in `telemetry.c` (drought, flood, split, fault, water skip/hold,
forecast stale, rain observed).

In:

```
PUMP:ON | PUMP:OFF | AUTO:ON | AUTO:OFF | RAIN:ON | RAIN:OFF
THRESH:low,high | WATER:secs | WATER:? | STATUS | RAINF:mm,hours
```

`WATER:120` runs up to 120 s (clamped to 5 min). `WATER:?` replies
`WATER:<left>`. Without trusted data the box drops to manual:
auto pump stops (`AUTO_OFF_FAULT`), `AUTO:ON` is rejected until sensors
recover. `PUMP:ON` still works as a manual override (you own the timing).

Debug flags in `config.h` (0 = off, compiled out): `DEBUG_HC05_PROBE`
(AT console at boot, phone disconnected), `DEBUG_BLE_SNIFF` (log raw bytes).

## Settings you will actually change

| Key | Default | Meaning |
|---|---|---|
| `SOIL_RAW_AIR` / `SOIL_RAW_WATER` | 506 / 200 | Calibrate with your probes (dry air / in water) |
| `THRESH_LOW_DEFAULT` / `THRESH_HIGH_DEFAULT` | 30 / 70 | Pump on below low, off at high |
| `RAIN_HOLD_CAP_Z_PCT` | 25 | Rain-hold cap |
| `RAIN_HUMIDITY_SKIP_THRESH` | 80 | Skip watering above this humidity + forecast |
| `RAIN_EMERGENCY_FLOOR_PCT` | 15 | Below this, water even if rain is forecast |
| `RELAY_ACTIVE_LOW` | 1 | 0 if your relay is active-high |
| `AUTO_RESUME_AFTER_FAULT` | 1 | 0 = stay manual after a fault |

Runtime commands `THRESH:low,high`, `RAINF:mm,hours`, `PUMP/AUTO/RAIN/WATER`
override without reflashing.

## Repo layout

- `irrigation_controller.ino` — scheduler + `main()`.
- `global.h` — one include for all modules.
- `default_config.h` — defaults. `config.h` (gitignored) overrides with plain `#define`.
- `sys.*` — boot, relay-safe init, fatal handler (relay off, halt).
- `gpio.*`, `adc.*`, `uart.*` — pins, soil ADC, USB debug channel.
- `timer0.*` + `swuart.*` — BLE UART 9600. `timer2.*` — `SYS_TICK`.
- `dht11.*` — temp/humidity driver. `soil_sensor.*` — mapping + vote.
- `climate_sensor.*` — DHT polling. `pump_control.*` — hysteresis + safety.
- `telemetry.*` — `DATA`/`ALERT` lines. `ble_comms.*` — command parser.
- `app_state.*` — shared state.
- `irrigation_gateway/` — Linux BLE gateway + PHP API + SQL schema.
- `irrigation_server_deprecated/` — old Python bridge, ignore it.

## Gateway (Linux)

Connects to the BLE module (name `Neko`), reads CSV telemetry, posts to
MySQL via REST. See `irrigation_gateway/api/README.md` for endpoints.

```sh
sudo apt install libcurl4-openssl-dev libcjson-dev libdbus-1-dev
cd irrigation_gateway
./compile.sh
./gateway
```

Three threads start by themselves: forecast fetch (hourly Open-Meteo →
`RAINF` over BLE), command poll (5 s, `/api/command/next` → BLE → ack),
config poll (30 s, `/api/config`).

## Data path

```
Sensors → Arduino (decides) → BLE UART → Gateway → MySQL ←→ Web UI
                ↓
              Pump (D7 relay)
```

1. Soil (2 s) + DHT11 (5 s) → Arduino vote + rain logic.
2. Arduino → gateway: `DATA,...` over BLE, 10 s or on change.
3. Gateway → MySQL: `POST /api/telemetry`.
4. Web UI → MySQL: `POST /api/config` (validated in PHP).
5. Gateway → Arduino: `THRESH:`, `RAINF:`, `PUMP:`, `AUTO:`, `WATER:` over BLE.
