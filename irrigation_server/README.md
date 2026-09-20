# Irrigation dashboard — local bridge server

A small local server that sits between the Arduino's BLE link and a
browser dashboard. It reads telemetry off the serial connection,
serves the dashboard, and forwards commands (pump on/off, auto/manual,
etc.) back down to the Arduino.

If no hardware is connected, it automatically falls back to generating
simulated readings — shaped exactly like the real telemetry — so you
can confirm the dashboard itself works before wiring anything up.

## 1. Pair the BLE module first

Before this script can see your Arduino, pair the BLE module (HC-05 /
HC-06) with your computer through the normal OS Bluetooth settings.
Once paired, it shows up as a serial port:

- **Windows**: Device Manager → Ports (COM & LPT) → something like `COM5`
- **macOS**: `/dev/tty.HC-05-DevB` (check `ls /dev/tty.*` after pairing)
- **Linux**: `/dev/rfcomm0` (may need `sudo rfcomm bind 0 <MAC address>` first)

Default pairing PIN for most HC-05/06 modules is `1234` or `0000`.

## 2. Install dependencies

```
pip install -r requirements.txt     # everything (serial + BLE + dashboard)
pip install flask                   # minimal: --ble or --simulate only.
                                    # pyserial is optional, used solely for --port mode.
```

## 3. Run it

```
python server.py                                # auto-detect a port
python server.py --port COM5                     # Windows, explicit
python server.py --port /dev/tty.HC-05-DevB       # macOS, explicit
python server.py --port /dev/rfcomm0              # Linux classic-SPP, explicit
python server.py --ble AA:BB:CC:DD:EE:FF          # Linux BLE via bluetoothctl
python server.py --simulate                       # force simulated data
```

## 1b. Linux BLE via bluetoothctl (no serial port)

If your module is a BLE (or dual-mode) JDY/HM-10-style board, the server
can talk to it directly over GATT - no `/dev/rfcomm0` needed. Everything
goes through the `bluetoothctl` CLI (BlueZ), driven automatically by
`server.py` + `ble_link.py`.

Prerequisites (on the Linux machine):

```
sudo apt install bluez        # provides bluetoothctl (>= 5.50 recommended)
bluetoothctl --version
```

Your user needs Bluetooth permission: run the server with `sudo`, or add
yourself to the `bluetooth` group (`sudo usermod -aG bluetooth $USER`,
then log out and back in). Make sure no phone is currently connected to
the module - it only takes one link at a time.

Find the module's MAC once (phone scanner app, or `bluetoothctl scan on`
and match the name/RSSI), then run:

```
python server.py --ble AA:BB:CC:DD:EE:FF
```

The server will: power the adapter on, trust + pair the module, connect,
select the UART characteristic (`0000ffe1-...`, override with
`--ble-char-uuid`), turn notifications on - and from then on everything
behaves exactly like serial mode: same dashboard, same `/api/command`
API. Incoming `DATA,...` lines stream in as notifications (a long line
can arrive split across several notifications; the server reassembles
them), and commands go out as hex-encoded `gatt.write` frames.

Useful flags:

```
--ble-char-uuid 0000ffe1-0000-1000-8000-00805f9b34fb   # UART characteristic
--ble-write-type request|command                       # try 'command' if writes get no reply
--ble-adapter hci0                                     # with several adapters
--ble-pin 1234                                         # if the module asks for a PIN
```

Troubleshooting:

- `bluetoothctl not found` - install BlueZ (see above).
- Pairing asks for a PIN - most modules accept `1234` (the default) or
  `0000`; pass it with `--ble-pin`.
- Commands sent but the Arduino never answers - retry with
  `--ble-write-type command` (some firmware only accepts
  write-without-response on the UART characteristic).
- `ERROR`/`NotPermitted` on notify - disconnect any phone first, or the
  characteristic UUID is wrong for your module (check manually:
  `bluetoothctl` -> `connect <MAC>` -> `menu gatt` ->
  `list-attributes`, then `attribute-info` on candidates).
- `--ble` needs no extra pip packages (`ble_link.py` is stdlib-only);
  `python ble_link.py --selftest` runs the notification-parser checks
  anywhere, no hardware needed.

Then open **http://localhost:5000** in a browser.

## What you'll see

- **Connection badge** (top right) — "Live — <port>" when reading real
  hardware, "Simulated data" when there's no serial connection, or an
  error state if the server itself isn't reachable.
- **Gauges** — soil moisture, temperature, humidity, pulled straight
  from the `DATA,...` telemetry line the Arduino sends.
- **Pump control** — Auto/Manual toggle and a manual pump switch. These
  send `AUTO:ON` / `AUTO:OFF` / `PUMP:ON` / `PUMP:OFF` over the same
  serial link the Arduino's `processCommand()` already handles.
- **Sensor health** — active sensor count, fault flag, rain flag (the
  three things the firmware actually reports; it doesn't transmit each
  sensor's individual raw percentage, only the voted/aggregate value).
- **Serial log** (collapsed panel at the bottom) — every raw line sent
  and received, useful for confirming the link is actually alive when
  you're bringing up real hardware for the first time.

## Known limitation: mode and thresholds aren't read back

The firmware's `sendTelemetry()` only sends: soil %, rain, pump state,
sensor fault, active count, temperature, humidity, and a probe-exclusion
bitmask (`probeMap`, bit `i` = probe `i` not voting). It does **not**
report back whether it's currently in Auto or Manual mode, or what the
current `threshLow`/`threshHigh` values are — those were only ever
meant to be set, not read. So:

- The Auto/Manual toggle shown here reflects the **last command this
  dashboard sent**, not a confirmed state from the Arduino. If another
  client changes it, or the Arduino resets, this can drift out of
  sync until you send a command from here again.
- Thresholds aren't shown live. You can still change them with
  `THRESH:low,high` — send it via curl for now:
  ```
  curl -X POST http://localhost:5000/api/command \
    -H "Content-Type: application/json" \
    -d '{"cmd":"THRESH:25,50"}'
  ```

If you want the dashboard to show these accurately, the fix is on the
firmware side: add `autoMode`, `threshLow`, and `threshHigh` to the
`sendTelemetry()` string and extend `parse_telemetry()` in `server.py`
to match. Happy to make that change if you want it.

## Files

- `server.py` — the bridge server (Flask + pyserial, or BLE via `ble_link.py`)
- `ble_link.py` — Linux BLE transport: drives `bluetoothctl` (BlueZ),
  stdlib-only, with a built-in `python ble_link.py --selftest` check
- `dashboard.html` — the dashboard UI, served by `server.py`
- `requirements.txt` — Python dependencies
