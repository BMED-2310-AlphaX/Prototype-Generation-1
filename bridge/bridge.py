#!/usr/bin/env python3
"""
AlphaX USB bridge — Arduino Uno/Nano + browser UI, no Bluetooth required.

WHAT THIS IS
    The Uno R3 has no radio, so the iPhone app cannot reach it. This puts the
    same interface in your Mac's browser instead and talks to the board over
    the USB cable you already have. Real hardware, real session logic, live
    UI — just wired instead of wireless.

HOW TO RUN
    1. Upload nano_session_controller.ino to the board
    2. CLOSE the Arduino Serial Monitor (only one program can hold the port)
    3. python3 bridge.py
    4. Your browser opens automatically

    No pip install. Standard library only, so nothing to set up.

HOW IT WORKS
    A background thread reads status lines from the serial port and keeps the
    most recent one. The page polls /status twice a second and POSTs commands
    to /cmd, which get written straight to the board. Same command protocol as
    the BLE version — START:<pct>:<min>, STOP, SET:<pct> — so the firmware is
    unchanged and nothing here is demo-specific.
"""

import glob
import json
import subprocess
import sys
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

BAUD = 115200
PORT = 8080

# Opening the port pulses DTR, which resets the Arduino. It needs a moment to
# come back before it will answer anything.
RESET_WAIT = 2.0

latest_status = {"state": "idle", "intensity": 0, "target": 40,
                 "remaining": 0, "bpm": 0, "rmssd": 0, "batt": 255}
status_lock = threading.Lock()
serial_file = None
log_lines = []


def find_port():
    """Locate the board. CH340 clones, FTDI, and genuine boards all differ."""
    patterns = ["/dev/cu.usbserial*", "/dev/cu.wchusbserial*",
                "/dev/cu.usbmodem*", "/dev/cu.SLAB_USBtoUART*"]
    found = []
    for p in patterns:
        found.extend(glob.glob(p))

    if not found:
        sys.exit(
            "No serial port found.\n\n"
            "  • Is the board plugged in?\n"
            "  • Is it a data cable, not charge-only?\n"
            "  • CH340 clone? Install the WCH CH34x driver.\n"
        )

    if len(found) > 1:
        print("Multiple ports found:")
        for i, p in enumerate(found):
            print(f"  [{i}] {p}")
        choice = input("Pick one [0]: ").strip() or "0"
        return found[int(choice)]

    return found[0]


def open_serial(path):
    """Configure and open the port using stty, so pyserial isn't needed."""
    subprocess.run(
        ["stty", "-f", path, str(BAUD), "cs8", "-cstopb", "-parenb",
         "raw", "-echo"],
        check=True)
    return open(path, "r+b", buffering=0)


def reader_thread():
    """Keep the newest status line. Ignore anything that isn't JSON."""
    global latest_status
    while True:
        try:
            raw = serial_file.readline()
        except Exception:
            time.sleep(0.2)
            continue

        if not raw:
            continue

        text = raw.decode("utf-8", errors="replace").strip()
        if not text:
            continue

        if text.startswith("{"):
            try:
                parsed = json.loads(text)
            except json.JSONDecodeError:
                continue
            with status_lock:
                latest_status = parsed
        else:
            # Boot banners and [cmd] echoes — useful in the page's log.
            log_lines.append(text)
            del log_lines[:-40]
            print("  board:", text)


def send(command):
    serial_file.write((command + "\n").encode())
    serial_file.flush()
    print("  sent :", command)


class Handler(BaseHTTPRequestHandler):

    def log_message(self, *args):
        pass                      # silence per-request noise

    def _send(self, code, body, ctype):
        encoded = body.encode() if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def do_GET(self):
        if self.path == "/":
            self._send(200, PAGE, "text/html; charset=utf-8")
        elif self.path == "/status":
            with status_lock:
                payload = dict(latest_status)
            payload["log"] = log_lines[-8:]
            self._send(200, json.dumps(payload), "application/json")
        else:
            self._send(404, "not found", "text/plain")

    def do_POST(self):
        if self.path != "/cmd":
            self._send(404, "not found", "text/plain")
            return
        length = int(self.headers.get("Content-Length", 0))
        command = self.rfile.read(length).decode().strip()
        if command:
            send(command)
        self._send(200, json.dumps({"ok": True}), "application/json")


PAGE = r"""<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>AlphaX Console</title>
<style>
 :root{--p:#4C1D6B;--pl:#8A4FB3;--pp:#F4F1F7;--b:#E3DAEA;--m:#5A5A5A;
       --r:#B23A54;--g:#2E7D5B}
 *{box-sizing:border-box}
 body{margin:0;padding:20px;background:#fff;color:#1a1a1a;max-width:620px;
      margin-inline:auto;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}
 h1{font-size:23px;margin:0 0 2px}
 .sub{color:var(--m);font-size:13px;margin-bottom:18px}
 .card{background:var(--pp);border:1px solid var(--b);border-radius:14px;
       padding:17px;margin-bottom:14px}
 .row{display:flex;align-items:center;justify-content:space-between;gap:10px}
 .dot{width:9px;height:9px;border-radius:50%;background:var(--g);
      display:inline-block;margin-right:7px}
 .timer{font-size:46px;font-weight:700;text-align:center;color:var(--p);
        font-variant-numeric:tabular-nums}
 .state{text-align:center;font-size:12px;color:var(--m);letter-spacing:1.2px;
        text-transform:uppercase;margin-top:2px}
 .metrics{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-top:14px}
 .metric{background:#fff;border:1px solid var(--b);border-radius:10px;
         padding:12px;text-align:center}
 .metric .v{font-size:23px;font-weight:700;color:var(--p);
            font-variant-numeric:tabular-nums}
 .metric .l{font-size:10px;color:var(--m);text-transform:uppercase;
            letter-spacing:.5px;margin-top:3px}
 canvas{width:100%;height:120px;display:block;background:#fff;
        border:1px solid var(--b);border-radius:10px;margin-top:12px}
 input[type=range]{width:100%;accent-color:var(--p);margin:10px 0 2px}
 button{font:inherit;font-weight:600;border-radius:10px;border:none;
        padding:13px 16px;cursor:pointer}
 .primary{background:var(--p);color:#fff;width:100%}
 .danger{background:var(--r);color:#fff;width:100%}
 .ghost{background:#fff;color:var(--p);border:1.5px solid var(--p)}
 .presets{display:flex;gap:8px;margin-top:8px}
 .presets button{flex:1;padding:10px;font-size:14px}
 .presets button.sel{background:var(--p);color:#fff}
 .hint{font-size:12px;color:var(--m);margin-top:8px}
 #log{font-family:ui-monospace,Menlo,monospace;font-size:11.5px;color:var(--m);
      max-height:130px;overflow-y:auto;white-space:pre-wrap;line-height:1.55;margin-top:8px}
</style></head><body>

<h1>AlphaX Console</h1>
<div class="sub">Live session control over USB &mdash; Arduino Uno / Nano</div>

<div class="card"><div class="row">
  <div><span class="dot"></span><span id="conn">Connected over USB</span></div>
  <span style="font-size:12px;color:var(--m)" id="rate">&mdash;</span>
</div></div>

<div class="card">
  <div class="timer" id="timer">--:--</div>
  <div class="state" id="state">idle</div>
  <div class="metrics">
    <div class="metric"><div class="v" id="bpm">--</div><div class="l">BPM</div></div>
    <div class="metric"><div class="v" id="hrv">--</div><div class="l">RMSSD ms</div></div>
    <div class="metric"><div class="v" id="int">--</div><div class="l">Intensity</div></div>
  </div>
  <canvas id="chart" width="640" height="240"></canvas>
  <div class="hint">RMSSD over the session &mdash; higher means more parasympathetic activity.</div>
</div>

<div class="card">
  <div class="row"><strong>Intensity</strong><span id="ilabel">40%</span></div>
  <input type="range" id="intensity" min="0" max="80" step="5" value="40">
  <div class="hint">Firmware caps output at 80%.</div>
  <div class="row" style="margin-top:16px"><strong>Duration</strong></div>
  <div class="presets" id="presets">
    <button class="ghost" data-m="1">1 min</button>
    <button class="ghost sel" data-m="5">5 min</button>
    <button class="ghost" data-m="20">20 min</button>
  </div>
</div>

<button class="primary" id="start">Start session</button>
<div style="height:10px"></div>
<button class="danger" id="stop">Stop</button>

<div class="card" style="margin-top:14px">
  <strong style="font-size:13px">Board output</strong>
  <div id="log">Waiting&hellip;</div>
</div>

<script>
let minutes = 5, hist = [], lastSeen = 0;
const $ = id => document.getElementById(id);

async function cmd(c){
  await fetch('/cmd', {method:'POST', body:c});
}

async function poll(){
  try{
    const d = await (await fetch('/status')).json();
    const m = Math.floor(d.remaining/60), s = d.remaining%60;
    $('timer').textContent = d.remaining > 0
      ? String(m).padStart(2,'0')+':'+String(s).padStart(2,'0') : '--:--';
    $('state').textContent = (d.state||'').replace('_',' ');
    $('bpm').textContent = d.bpm   > 0 ? Math.round(d.bpm)   : '--';
    $('hrv').textContent = d.rmssd > 0 ? Math.round(d.rmssd) : '--';
    $('int').textContent = d.intensity + '%';
    $('rate').textContent = new Date().toLocaleTimeString();

    if (d.rmssd > 0){
      hist.push(d.rmssd);
      if (hist.length > 240) hist.shift();
      draw();
    }
    if (d.log && d.log.length) $('log').textContent = d.log.slice().reverse().join('\n');
  }catch(e){
    $('conn').textContent = 'Bridge not responding';
  }
}

function draw(){
  const c = $('chart'), x = c.getContext('2d'), W = c.width, H = c.height, p = 16;
  x.clearRect(0,0,W,H);
  if (hist.length < 2) return;
  const lo = Math.min(...hist), hi = Math.max(...hist), span = (hi-lo)||1;
  x.strokeStyle = '#E3DAEA'; x.lineWidth = 1;
  for (let i=0;i<=3;i++){
    const y = p+(H-2*p)*i/3;
    x.beginPath(); x.moveTo(p,y); x.lineTo(W-p,y); x.stroke();
  }
  x.strokeStyle = '#4C1D6B'; x.lineWidth = 2.5; x.beginPath();
  hist.forEach((v,i)=>{
    const px = p+(W-2*p)*i/(hist.length-1);
    const py = H-p-((v-lo)/span)*(H-2*p);
    i ? x.lineTo(px,py) : x.moveTo(px,py);
  });
  x.stroke();
  x.fillStyle = '#5A5A5A'; x.font = '12px sans-serif';
  x.fillText(Math.round(hi)+' ms', p+3, p+11);
  x.fillText(Math.round(lo)+' ms', p+3, H-p-3);
}

$('start').onclick = () => cmd('START:'+$('intensity').value+':'+minutes);
$('stop').onclick  = () => cmd('STOP');
$('intensity').oninput  = e => $('ilabel').textContent = e.target.value+'%';
$('intensity').onchange = e => cmd('SET:'+e.target.value);
$('presets').onclick = e => {
  if (e.target.tagName !== 'BUTTON') return;
  [...$('presets').children].forEach(b=>b.classList.remove('sel'));
  e.target.classList.add('sel');
  minutes = parseInt(e.target.dataset.m,10);
};

setInterval(poll, 500);
poll();
</script></body></html>
"""


def main():
    global serial_file

    path = find_port()
    print(f"\nAlphaX bridge\n  port : {path}\n  baud : {BAUD}")

    serial_file = open_serial(path)
    print(f"  wait : {RESET_WAIT}s for the board to reset…")
    time.sleep(RESET_WAIT)

    threading.Thread(target=reader_thread, daemon=True).start()

    url = f"http://localhost:{PORT}"
    print(f"  page : {url}\n\nCtrl-C to stop.\n")
    threading.Timer(1.0, lambda: webbrowser.open(url)).start()

    try:
        ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
    except KeyboardInterrupt:
        print("\nStopping — sending STOP to the board.")
        try:
            send("STOP")
        except Exception:
            pass


if __name__ == "__main__":
    main()
