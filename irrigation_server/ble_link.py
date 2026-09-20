"""BLE UART link to the irrigation node, driven through `bluetoothctl`.

Linux + BlueZ only. Spawns a `bluetoothctl` subprocess and drives it over
stdin/stdout::

    power on -> agent/pair/trust -> connect <MAC> -> menu gatt ->
    select-attribute <UART char UUID> -> notify on

Incoming GATT notifications are printed by bluetoothctl as hex dumps
("Value:" blocks, possibly split across several notifications when a
telemetry line exceeds the ATT MTU). They are decoded, reassembled into
``\\n``-terminated lines and handed to ``on_line`` - the exact same line
protocol the Arduino sends over classic SPP serial, so the rest of the
server treats both transports identically. Outgoing text is hex-encoded
and sent with ``gatt.write``.

Only the Python standard library is used (subprocess/threading/re), so
this adds no pip dependencies. The Linux host needs BlueZ instead:
``sudo apt install bluez`` (bluetoothctl >= 5.50 recommended).

Typical JDY/HM-10 UART: service 0000ffe0-..., characteristic 0000ffe1-....
"""

import re
import shutil
import subprocess
import threading
import time

BLE_UART_CHAR_UUID = "0000ffe1-0000-1000-8000-00805f9b34fb"

_ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
_HEXTOK_RE = re.compile(r"^[0-9A-Fa-f]{2}$")


def encode_write(text):
    """'PUMP:ON' -> '50 55 4D 50 3A 4F 4E 0A' for ``gatt.write``.

    A trailing newline is appended when missing: the firmware only
    executes a command once it sees \\n or \\r.
    """
    if not text.endswith("\n"):
        text += "\n"
    return " ".join(f"{b:02X}" for b in text.encode("utf-8", errors="ignore"))


def leading_hex_bytes(text):
    """Decode the leading run of 'AA BB ..' hex tokens; stop at the first
    non-hex token (this skips the ASCII rendering bluetoothctl appends
    after the hex dump on the same line)."""
    out = bytearray()
    for tok in text.split():
        if _HEXTOK_RE.match(tok):
            out.append(int(tok, 16))
        else:
            break
    return bytes(out)


class LineAssembler:
    """Turn an arbitrary byte stream into \\n-terminated text lines
    (same semantics as serial.readline().strip(): CR dropped, empty
    lines skipped)."""

    def __init__(self, on_line):
        self._buf = bytearray()
        self._on_line = on_line

    def feed(self, data):
        for b in data:
            if b == 0x0A:  # \n completes a line
                line = bytes(self._buf).decode("utf-8", errors="ignore").strip()
                del self._buf[:]
                if line:
                    self._on_line(line)
            elif b != 0x0D:  # drop \r, keep everything else
                self._buf.append(b)


class BluetoothctlLink:
    """Owns one bluetoothctl session to a single MAC. Run run_forever()
    in a thread; it reconnects until stop() is called."""

    def __init__(self, mac, on_line, on_state=None,
                 char_uuid=BLE_UART_CHAR_UUID, adapter=None,
                 pin="1234", write_type="request"):
        self.mac = mac.upper()
        self.char_uuid = char_uuid.lower()
        self.adapter = adapter
        self.pin = pin
        self.write_type = write_type
        self._assembler = LineAssembler(on_line)
        self._on_state = on_state or (lambda connected: None)
        self._proc = None
        self._rx_text = ""
        self._rx_lock = threading.Lock()
        self._io_lock = threading.Lock()
        self._stop = threading.Event()
        self._value_armed = False

    # ---------------- process plumbing ----------------
    def _spawn(self):
        if shutil.which("bluetoothctl") is None:
            raise RuntimeError(
                "bluetoothctl not found - install BlueZ (sudo apt install bluez)")
        self._proc = subprocess.Popen(
            ["bluetoothctl"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, bufsize=1,
            errors="replace",
        )
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        try:
            for raw in self._proc.stdout:
                line = _ANSI_RE.sub("", raw).rstrip("\n")
                with self._rx_lock:
                    self._rx_text = (self._rx_text + line + "\n")[-30000:]
                self._handle_line(line)
        except Exception:
            pass

    def _handle_line(self, line):
        # Auto-answer legacy PIN and LE passkey pairing prompts with the
        # configured PIN (override with --ble-pin on the server CLI).
        low = line.lower()
        if "request pin code" in low or "enter passkey" in low:
            self._send(self.pin)
            return
        if re.search(r"value\s*:", line, re.IGNORECASE):
            # Start of a notification/readout hex block. Only bytes from
            # such blocks enter the stream, so MAC addresses and object
            # paths elsewhere in the output can never pollute it.
            self._value_armed = True
            after = re.split(r"value\s*:",
                             line, maxsplit=1, flags=re.IGNORECASE)[1]
            self._assembler.feed(leading_hex_bytes(after))
        elif self._value_armed:
            if line[:1].isspace():
                self._assembler.feed(leading_hex_bytes(line))  # dump continues
            else:
                self._value_armed = False

    def _send(self, cmd):
        with self._io_lock:
            if self._proc is None or self._proc.poll() is not None:
                raise RuntimeError("bluetoothctl process is gone")
            self._proc.stdin.write(cmd + "\n")
            self._proc.stdin.flush()

    def _recent(self):
        with self._rx_lock:
            return self._rx_text

    def _clear_rx(self):
        with self._rx_lock:
            self._rx_text = ""
        self._value_armed = False

    def _expect(self, pattern, timeout):
        rx = re.compile(pattern, re.IGNORECASE)
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if self._stop.is_set():
                return False
            if rx.search(self._recent()):
                return True
            time.sleep(0.2)
        return False

    # ---------------- session ----------------
    def _round(self):
        """One connect+subscribe attempt. True once notifications flow."""
        self._clear_rx()
        self._send("back")  # ensure main menu (harmless if already there)
        time.sleep(0.3)
        if self.adapter:
            self._send(f"select {self.adapter}")
            time.sleep(0.5)
        self._send("power on")
        time.sleep(1.0)
        # Keyboard-capable agent: covers Just Works (no prompt appears)
        # AND the passkey-entry flow this module uses (auto-answered with
        # --ble-pin). NoInputNoOutput cannot enter a code, so it is wrong
        # here now that bonding is confirmed necessary.
        self._send("agent on")
        time.sleep(0.5)
        self._send("default-agent")
        time.sleep(0.5)
        self._send("pairable on")
        time.sleep(0.5)
        self._send("scan on")
        time.sleep(2.0)
        for attempt in range(1, 4):
            print(f"[ble_link] connect attempt {attempt}/3 to {self.mac} ...")
            self._send(f"connect {self.mac}")
            if self._expect(r"Connection successful|Already connected|"
                               r"Connected:\s*yes", 25):
                break
            time.sleep(2.0)
        else:
            print("[ble_link] connect timed out, retrying round")
            return False
        print("[ble_link] connected, selecting UART characteristic ...")
        self._send("scan off")
        self._send(f"trust {self.mac}")
        time.sleep(0.5)
        # Pair only when not already bonded: re-pairing a bonded device can
        # disturb the good bond (and may raise a passkey prompt answered
        # here with a wrong auto-PIN).
        self._send(f"info {self.mac}")
        time.sleep(1.0)
        if not self._expect(r"Paired:\s*yes", 3):
            print("[ble_link] not bonded, pairing ...")
            self._send(f"pair {self.mac}")
            if self._expect(r"Pairing successful", 15):
                print("[ble_link] pairing successful")
            else:
                print("[ble_link] pairing did not succeed")
            time.sleep(1.0)
        else:
            print("[ble_link] already bonded, skipping pair")
        for _ in range(4):
            self._send("menu gatt")
            time.sleep(0.3)
            self._send(f"select-attribute {self.char_uuid}")
            time.sleep(0.5)
            self._send("attribute-info")
            if self._expect(r"ffe1", 4):
                break
            time.sleep(2.0)
        else:
            print("[ble_link] UART characteristic not found, retrying round")
            return False
        self._send("notify on")
        if self._expect(r"Notify started", 8):
            print("[ble_link] notifications on - link live")
            return True
        print("[ble_link] notify failed, retrying round")
        return False

    def _monitor(self):
        """Block until disconnect/stop. True means reconnect."""
        while not self._stop.is_set():
            if self._proc is None or self._proc.poll() is not None:
                return True
            if re.search(r"Connected:\s*no", self._recent(), re.IGNORECASE):
                return True
            time.sleep(1.0)
        return False

    def _teardown(self):
        proc, self._proc = self._proc, None
        if proc is None:
            return
        try:
            try:
                proc.stdin.write("quit\n")
                proc.stdin.flush()
            except Exception:
                pass
            proc.wait(timeout=3)
        except Exception:
            try:
                proc.kill()
            except Exception:
                pass
        for stream in (proc.stdin, proc.stdout):
            try:
                stream.close()
            except Exception:
                pass

    def run_forever(self):
        while not self._stop.is_set():
            try:
                self._spawn()
                if self._round():
                    self._on_state(True)
                    reconnect = self._monitor()
                    self._on_state(False)
                    self._teardown()
                    if not reconnect:
                        break
                else:
                    self._teardown()
            except Exception as e:
                print(f"[ble_link] round failed: {e}")
                self._teardown()
            if not self._stop.is_set():
                time.sleep(5)

    def write_line(self, text):
        """Send one command line to the Arduino (newline appended)."""
        payload = encode_write(text)
        if self.write_type == "command":
            self._send(f'write "{payload}" 0 command')
        else:
            self._send(f'write "{payload}"')

    def stop(self):
        self._stop.set()
        self._teardown()


def _selftest():
    """Parser/encoder checks. Runs anywhere, no hardware needed."""
    assert encode_write("PUMP:ON") == "50 55 4D 50 3A 4F 4E 0A", encode_write("PUMP:ON")
    assert encode_write("STATUS\n") == "53 54 41 54 55 53 0A"
    assert leading_hex_bytes("44 41 54 41 DATA,42.") == b"DATA"
    assert leading_hex_bytes("") == b""

    got = []
    asm = LineAssembler(got.append)
    asm.feed(b"DATA,42,0")
    assert got == [], got
    asm.feed(b",0,0,3,25.3,48.1\r\nDATA")
    assert got == ["DATA,42,0,0,0,3,25.3,48.1"], got

    # Full bluetoothctl transcript: split ATT payloads, ASCII trailers,
    # prompts and MAC object paths (which must NOT pollute the stream).
    lines2 = []
    link = BluetoothctlLink("AA:BB:CC:DD:EE:FF", lines2.append)
    dev = "/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF"
    transcript = [
        "[bluetooth]# power on",
        "Changing power on succeeded",
        "[bluetooth]# connect AA:BB:CC:DD:EE:FF",
        "Attempting to connect to AA:BB:CC:DD:EE:FF",
        "[CHG] Device AA:BB:CC:DD:EE:FF Connected: yes",
        "Connection successful",
        f"[NEW] Service {dev}/service0010 Vendor specific",
        f"[NEW] Characteristic {dev}/service0010/char0011 Vendor specific",
        "[JDY:/service0010/char0011]# notify on",
        f"[CHG] Attribute {dev}/service0010/char0011 Notifying: yes",
        "Notify started",
        # one DATA line split across two notifications (ATT MTU 23)
        f"[CHG] Attribute {dev}/service0010/char0011 Value:",
        "  44 41 54 41 2c 34 32 2c 30 2c 30 2c 30 2c 33 2c    DATA,42,0,0,0,3,",
        f"[CHG] Attribute {dev}/service0010/char0011 Value:",
        "  32 35 2e 33 2c 34 38 2e 31 0a                      25.3,48.1.",
        "[JDY:/service0010/char0011]# ",
        # single-line Value form
        f"[CHG] Attribute {dev}/service0010/char0011 Value: "
        "41 4C 45 52 54 2C 44 52 4F 55 47 48 54 0A",
    ]
    for ln in transcript:
        link._handle_line(ln)
    assert lines2 == ["DATA,42,0,0,0,3,25.3,48.1", "ALERT,DROUGHT"], lines2
    print("ble_link selftest: OK")


if __name__ == "__main__":
    import sys as _sys
    if "--selftest" in _sys.argv:
        _selftest()
    else:
        print(__doc__)
