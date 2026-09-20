"""
Local bridge server for the smart irrigation dashboard.

What it does:
  - Opens the BLE module's serial link (HC-05/HC-06 show up as a normal
    COM port on Windows, or /dev/tty.* on macOS/Linux, once paired)
  - ...or, on Linux with --ble <MAC>, talks BLE directly through the
    `bluetoothctl` CLI (BlueZ): subscribes to the UART characteristic's
    notifications for telemetry and sends commands with `gatt.write`.
    Same line protocol, same dashboard - only the transport differs.
  - Parses the DATA,... telemetry lines the Arduino sends and keeps
    the latest reading + a rolling history in memory
  - Serves the dashboard at http://localhost:5000
  - Forwards commands (PUMP:ON, AUTO:OFF, etc.) from the dashboard
    down to the Arduino over the same serial link
  - Polls Open-Meteo for rain forecast and sends RAINF:<mm>,<hours> to firmware

If no serial port is available -- no hardware plugged in, not paired
yet, or pyserial isn't installed -- it falls back to generating
simulated readings instead, so you can confirm the dashboard itself
works before wiring up real hardware. The dashboard behaves exactly
the same either way; it just polls this server, it doesn't know or
care whether the data is real.

Setup:
    pip install -r requirements.txt

Run:
    python server.py                              # auto-detect a serial port
    python server.py --port COM5                   # Windows, explicit port
    python server.py --port /dev/tty.HC-05-DevB     # macOS/Linux, explicit port
    python server.py --simulate                     # force simulated data
    python server.py --ble AA:BB:CC:DD:EE:FF        # Linux BLE via bluetoothctl

Then open http://localhost:5000 in a browser.
"""

import argparse
import json
import random
import threading
import time
from collections import deque
from pathlib import Path

from flask import Flask, jsonify, request, send_from_directory

try:
    from ble_link import BluetoothctlLink, BLE_UART_CHAR_UUID
    BLE_LINK_AVAILABLE = True
except ImportError:
    BluetoothctlLink = None
    BLE_UART_CHAR_UUID = "0000ffe1-0000-1000-8000-00805f9b34fb"
    BLE_LINK_AVAILABLE = False

try:
    import serial
    import serial.tools.list_ports
    PYSERIAL_AVAILABLE = True
except ImportError:
    PYSERIAL_AVAILABLE = False

try:
    import requests
    REQUESTS_AVAILABLE = True
except ImportError:
    REQUESTS_AVAILABLE = False

APP_DIR = Path(__file__).parent
BAUD_RATE = 9600
HISTORY_LEN = 50
LOG_LEN = 100

# Open-Meteo configuration
OPENMETEO_LAT = 2.92    # Cyberjaya, Malaysia; override with --lat/--lon
OPENMETEO_LON = 101.66
OPENMETEO_POLL_INTERVAL = 3600  # 1 hour
RAIN_FORECAST_HOURS = 12        # lookahead window (matches firmware RAIN_FORECAST_X_HOURS)
RAIN_FORECAST_MM = 2            # threshold (matches firmware RAIN_FORECAST_Y_MM)

app = Flask(__name__, static_folder=None)

state_lock = threading.Lock()
state = {
    "connected": False,
    "simulated": False,
    "port": None,
    "latest": None,
    "history": deque(maxlen=HISTORY_LEN),
    "log": deque(maxlen=LOG_LEN),
    # The firmware doesn't report its own auto/manual mode back over
    # telemetry, so this tracks the last mode *this dashboard* asked
    # for. If the Arduino was switched by another client, this can
    # drift out of sync until the next command is sent from here.
    "assumed_auto_mode": True,
    # Rain forecast state
    "rain_forecast": None,       # {"mm": float, "hours": int, "fetched": timestamp}
}

serial_conn = None  # the open serial.Serial object, if any
ble_link = None  # the BluetoothctlLink object, if --ble mode is used


def log_line(text, direction):
    with state_lock:
        state["log"].append({"t": time.time(), "dir": direction, "text": text})


def parse_telemetry(line):
    """DATA,<soilPct>,<rain>,<pump>,<fault>,<activeCount>,<tempC>,<humPct>[,<probeMap>[,<waterLeft>]]
    probeMap bit i = probe i excluded (absent on old firmware).
    waterLeft = seconds left on a timed run (absent on old firmware)."""
    parts = line.split(",")
    if len(parts) not in (8, 9, 10) or parts[0] != "DATA":
        return None
    try:
        def parse_float(s):
            return None if s == "NA" else float(s)

        return {
            "t": time.time(),
            "soilPct": int(parts[1]),
            "rainDetected": parts[2] == "1",
            "pumpState": parts[3] == "1",
            "sensorFault": parts[4] == "1",
            "activeCount": int(parts[5]),
            "tempC": parse_float(parts[6]),
            "humPct": parse_float(parts[7]),
            "probeMap": int(parts[8]) if len(parts) >= 9 else None,
            "waterLeft": int(parts[9]) if len(parts) == 10 else None,
        }
    except (ValueError, IndexError):
        return None


def push_reading(reading, simulated):
    with state_lock:
        state["latest"] = reading
        state["history"].append(reading)
        state["simulated"] = simulated


def find_default_port():
    if not PYSERIAL_AVAILABLE:
        return None
    ports = list(serial.tools.list_ports.comports())
    for p in ports:
        desc = (p.description or "").lower()
        if "bluetooth" in desc or "hc-05" in desc or "hc-06" in desc:
            return p.device
    return ports[0].device if ports else None


def fetch_openmeteo_forecast(lat, lon):
    """Fetch rain forecast from Open-Meteo (no API key needed).
    Returns (total_mm, hours_ahead) for next RAIN_FORECAST_HOURS, or None on error."""
    if not REQUESTS_AVAILABLE:
        return None
    url = "https://api.open-meteo.com/v1/forecast"
    params = {
        "latitude": lat,
        "longitude": lon,
        "hourly": "precipitation",
        "forecast_hours": RAIN_FORECAST_HOURS,
        "timezone": "UTC",
    }
    try:
        resp = requests.get(url, params=params, timeout=10)
        resp.raise_for_status()
        data = resp.json()
        hourly = data.get("hourly", {})
        precip = hourly.get("precipitation", [])
        if not precip:
            return None
        # Sum precipitation over the lookahead window
        total_mm = sum(precip[:RAIN_FORECAST_HOURS])
        # Find first hour with precip > 0 for "hours ahead" hint
        hours_ahead = RAIN_FORECAST_HOURS
        for i, p in enumerate(precip[:RAIN_FORECAST_HOURS]):
            if p > 0:
                hours_ahead = i + 1
                break
        return (round(total_mm, 1), hours_ahead)
    except Exception as e:
        print(f"[server] Open-Meteo fetch error: {e}")
        return None


def rain_forecaster(lat, lon):
    """Background thread: polls Open-Meteo hourly, sends RAINF on change."""
    last_sent = None  # (mm, hours) tuple
    first_send_ok = False

    # Fetch immediately on startup; retry every 60s until first successful send
    while not first_send_ok:
        forecast = fetch_openmeteo_forecast(lat, lon)
        if forecast is not None:
            mm, hours = forecast
            mm = int(mm)  # truncate (floor for non-negative) — understate rain per asymmetry
            now = time.time()
            with state_lock:
                state["rain_forecast"] = {"mm": mm, "hours": hours, "fetched": now}
            cmd = f"RAINF:{mm},{hours}"
            with state_lock:
                connected = state["connected"]
            if connected and (serial_conn is not None or ble_link is not None):
                try:
                    if serial_conn is not None:
                        serial_conn.write((cmd + "\n").encode())
                    elif ble_link is not None:
                        ble_link.write_line(cmd)
                    log_line(cmd, "tx")
                    print(f"[server] sent {cmd}")
                    first_send_ok = True
                    last_sent = (mm, hours)
                except Exception as e:
                    print(f"[server] write error: {e}")
        if not first_send_ok:
            time.sleep(60)  # short retry until board connects

    # Hourly polling loop
    while True:
        time.sleep(OPENMETEO_POLL_INTERVAL)
        forecast = fetch_openmeteo_forecast(lat, lon)
        if forecast is None:
            # Keep last cached forecast on API failure
            with state_lock:
                cached = state.get("rain_forecast")
            if cached:
                print("[server] Open-Meteo failed, using cached forecast")
            else:
                print("[server] Open-Meteo failed, no cached forecast")
            continue

        mm, hours = forecast
        mm = int(mm)  # truncate (floor for non-negative) — understate rain per asymmetry
        now = time.time()
        with state_lock:
            state["rain_forecast"] = {"mm": mm, "hours": hours, "fetched": now}

        # Send RAINF on change OR hourly resend to keep firmware TTL fresh (2h TTL, 1h poll)
        cmd = f"RAINF:{mm},{hours}"
        with state_lock:
            connected = state["connected"]
        if connected and (serial_conn is not None or ble_link is not None):
            try:
                if serial_conn is not None:
                    serial_conn.write((cmd + "\n").encode())
                elif ble_link is not None:
                    ble_link.write_line(cmd)
                log_line(cmd, "tx")
                print(f"[server] sent {cmd}" if last_sent != (mm, hours) else f"[server] resend {cmd} (keepalive)")
            except Exception as e:
                print(f"[server] write error: {e}")
        last_sent = (mm, hours)


def simulate_forever():
    """Generates readings shaped exactly like real telemetry, so the
    dashboard can be tested end-to-end before hardware is attached."""
    last_soil = random.uniform(35, 55)
    pump_on = False
    while True:
        with state_lock:
            auto = state["assumed_auto_mode"]

        drift = random.uniform(-6, 5 if pump_on else -3)
        last_soil = max(3, min(97, last_soil + drift))
        fault = random.random() < 0.06
        active = 1 if fault else 3
        probe_map = (1 << random.randint(0, 2)) if fault else 0

        if auto and not fault:
            if not pump_on and last_soil < 20:
                pump_on = True
            elif pump_on and last_soil >= 45:
                pump_on = False

        reading = {
            "t": time.time(),
            "soilPct": round(last_soil),
            "rainDetected": False,
            "pumpState": pump_on,
            "sensorFault": fault,
            "activeCount": active,
            "probeMap": probe_map,
            "waterLeft": random.randint(0, 180) if pump_on else 0,
            "tempC": round(random.uniform(27, 33), 1),
            "humPct": round(random.uniform(55, 85)),
        }
        push_reading(reading, simulated=True)
        time.sleep(3)


def serial_worker(port_name):
    global serial_conn
    while True:
        try:
            with serial.Serial(port_name, BAUD_RATE, timeout=1) as ser:
                serial_conn = ser
                with state_lock:
                    state["connected"] = True
                    state["port"] = port_name
                    state["simulated"] = False
                print(f"[server] connected to {port_name}")
                while True:
                    raw = ser.readline().decode(errors="ignore").strip()
                    if not raw:
                        continue
                    log_line(raw, "rx")
                    reading = parse_telemetry(raw)
                    if reading:
                        push_reading(reading, simulated=False)
        except Exception as e:
            print(f"[server] serial error on {port_name}: {e}")
            with state_lock:
                state["connected"] = False
            serial_conn = None
            time.sleep(3)  # retry


def ble_worker(mac, char_uuid, write_type, adapter, pin):
    """BLE transport via bluetoothctl (Linux + BlueZ). Same line protocol
    as serial_worker: every complete line goes to the log + parser, so
    the dashboard cannot tell the transports apart."""
    global ble_link
    mac = mac.upper()

    def on_line(raw):
        if not raw:
            return
        log_line(raw, "rx")
        reading = parse_telemetry(raw)
        if reading:
            push_reading(reading, simulated=False)

    def on_state(connected):
        with state_lock:
            state["connected"] = connected
            if connected:
                state["port"] = f"BLE {mac}"
                state["simulated"] = False

    while True:
        try:
            link = BluetoothctlLink(
                mac, on_line, on_state,
                char_uuid=char_uuid, adapter=adapter, pin=pin,
                write_type=write_type,
            )
            ble_link = link
            print(f"[server] starting BLE link to {mac} via bluetoothctl")
            link.run_forever()
        except Exception as e:
            print(f"[server] BLE link error: {e}")
        with state_lock:
            state["connected"] = False
        ble_link = None
        time.sleep(5)  # retry


@app.route("/")
def index():
    return send_from_directory(APP_DIR, "dashboard.html")


@app.route("/api/status")
def api_status():
    with state_lock:
        return jsonify({
            "connected": state["connected"],
            "simulated": state["simulated"],
            "port": state["port"],
            "latest": state["latest"],
            "history": list(state["history"]),
            "assumedAutoMode": state["assumed_auto_mode"],
            "rainForecast": state.get("rain_forecast"),
        })


@app.route("/api/log")
def api_log():
    with state_lock:
        return jsonify(list(state["log"]))


@app.route("/api/ports")
def api_ports():
    if not PYSERIAL_AVAILABLE:
        return jsonify([])
    return jsonify([
        {"device": p.device, "description": p.description}
        for p in serial.tools.list_ports.comports()
    ])


@app.route("/api/command", methods=["POST"])
def api_command():
    body = request.get_json(silent=True) or {}
    cmd = (body.get("cmd") or "").strip()
    if not cmd:
        return jsonify({"ok": False, "error": "missing cmd"}), 400

    if cmd in ("AUTO:ON", "AUTO:OFF", "PUMP:ON", "PUMP:OFF"):
        with state_lock:
            if cmd == "AUTO:ON":
                state["assumed_auto_mode"] = True
            elif cmd == "AUTO:OFF":
                state["assumed_auto_mode"] = False
            else:
                state["assumed_auto_mode"] = False  # PUMP:* is a manual override

    with state_lock:
        connected = state["connected"]

    if connected and serial_conn is not None:
        try:
            serial_conn.write((cmd + "\n").encode())
            log_line(cmd, "tx")
            return jsonify({"ok": True, "mode": "hardware"})
        except Exception as e:
            return jsonify({"ok": False, "error": str(e)}), 500
    elif connected and ble_link is not None:
        try:
            ble_link.write_line(cmd)
            log_line(cmd, "tx")
            return jsonify({"ok": True, "mode": "hardware"})
        except Exception as e:
            return jsonify({"ok": False, "error": str(e)}), 500
    else:
        # No hardware attached: apply the command to the simulated
        # state directly so the dashboard still responds to it.
        with state_lock:
            if state["latest"] is not None:
                if cmd == "PUMP:ON":
                    state["latest"]["pumpState"] = True
                elif cmd == "PUMP:OFF":
                    state["latest"]["pumpState"] = False
        log_line(cmd, "tx-sim")
        return jsonify({"ok": True, "mode": "simulated"})


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", help="Serial port, e.g. COM5 or /dev/tty.HC-05")
    parser.add_argument("--simulate", action="store_true", help="Force simulated data")
    parser.add_argument("--ble", metavar="MAC",
                        help="Linux BLE via bluetoothctl, e.g. AA:BB:CC:DD:EE:FF")
    parser.add_argument("--ble-char-uuid", default=BLE_UART_CHAR_UUID,
                        help="BLE UART characteristic UUID (default: %(default)s)")
    parser.add_argument("--ble-write-type", choices=["request", "command"],
                        default="request",
                        help="GATT write type (try 'command' if writes get no reply)")
    parser.add_argument("--ble-adapter", default=None,
                        help="Bluetooth adapter, e.g. hci0 (default: BlueZ default)")
    parser.add_argument("--ble-pin", default="1234",
                        help="Pairing PIN if the module asks for one")
    parser.add_argument("--http-port", type=int, default=5000)
    parser.add_argument("--lat", type=float, default=OPENMETEO_LAT, help="Latitude for Open-Meteo")
    parser.add_argument("--lon", type=float, default=OPENMETEO_LON, help="Longitude for Open-Meteo")
    args = parser.parse_args()

    use_serial = PYSERIAL_AVAILABLE and not args.simulate and not args.ble
    port_name = args.port or (find_default_port() if use_serial else None)

    if args.ble and not args.simulate:
        if not BLE_LINK_AVAILABLE:
            print("[server] ble_link.py not found next to server.py - cannot use --ble")
            threading.Thread(target=simulate_forever, daemon=True).start()
        else:
            threading.Thread(
                target=ble_worker,
                args=(args.ble, args.ble_char_uuid, args.ble_write_type,
                      args.ble_adapter, args.ble_pin),
                daemon=True,
            ).start()
    elif use_serial and port_name:
        print(f"[server] starting serial worker on {port_name}")
        threading.Thread(target=serial_worker, args=(port_name,), daemon=True).start()
    else:
        if not PYSERIAL_AVAILABLE:
            print("[server] pyserial not installed - running in simulated mode")
        elif args.simulate:
            print("[server] --simulate passed - running in simulated mode")
        else:
            print("[server] no serial port found - running in simulated mode")
        threading.Thread(target=simulate_forever, daemon=True).start()

    # Start rain forecaster if requests available
    if REQUESTS_AVAILABLE:
        print(f"[server] starting rain forecaster for lat={args.lat}, lon={args.lon}")
        threading.Thread(target=rain_forecaster, args=(args.lat, args.lon), daemon=True).start()
    else:
        print("[server] requests not installed - rain forecast disabled")

    print(f"[server] dashboard at http://localhost:{args.http_port}")
    app.run(host="0.0.0.0", port=args.http_port, debug=False)


if __name__ == "__main__":
    main()