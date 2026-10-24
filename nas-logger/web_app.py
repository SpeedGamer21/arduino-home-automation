#!/usr/bin/env python3
"""Home automation web app.

Serves a dashboard (relays, temperature, humidity, connection status) and a
browser for the daily log files written by nas_logger.py. The dashboard talks
straight to the controller over the LAN (UDP), so it keeps working when Blynk
or the internet is down.

Settings via environment variables:
  LOG_DIR       folder with the daily .txt files     (default: ./logs)
  WEB_PORT      HTTP port                            (default: 8080)
  WEB_USER      login for the web page; set both when DEVICE_IP is set,
  WEB_PASSWORD  since the page can open the gate / garage
  DEVICE_IP     the controller's IP address; without it the page shows the
                logs only
  DEVICE_KEY    same value as LOCAL_CONTROL_KEY in secrets.h
  DEVICE_PORT   the controller's UDP port            (default: 5140, logPort)
  RELAY_NAMES   comma-separated names for relays 1-8 (default: Switch 1..8)
"""
import hmac
import io
import json
import math
import os
import random
import re
import secrets
import socket
import sys
import threading
import time
import zipfile
from http.cookies import CookieError, SimpleCookie
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

LOG_DIR = os.environ.get("LOG_DIR", "logs")
PORT = int(os.environ.get("WEB_PORT", "8080"))
WEB_USER = os.environ.get("WEB_USER", "")
WEB_PASSWORD = os.environ.get("WEB_PASSWORD", "")
DEVICE_IP = os.environ.get("DEVICE_IP", "")
DEVICE_KEY = os.environ.get("DEVICE_KEY", "")
DEVICE_PORT = int(os.environ.get("DEVICE_PORT", "5140"))
STATIC_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "static")

TOTAL_RELAYS = 8
MOMENTARY_RELAYS = 4  # relays 1-4 pulse, relays 5-8 toggle
_names = [n.strip() for n in os.environ.get("RELAY_NAMES", "").split(",")]
RELAY_NAMES = [(_names[i] if i < len(_names) and _names[i] else f"Switch {i + 1}")
               for i in range(TOTAL_RELAYS)]

DAY_RE = re.compile(r"\d{4}-\d{2}-\d{2}")
LINE_RE = re.compile(r"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})  (.*)")
MAX_LIMIT = 2000
STATUS_CACHE_SECONDS = 3  # several open pages share one request to the controller
REPLY_TIMEOUT = 0.6       # per attempt; the controller answers within a loop pass
ATTEMPTS = 3
SESSION_SECONDS = 30 * 24 * 3600
STATIC_FILES = {"/": "index.html", "/style.css": "style.css"}
PUBLIC_PATHS = {"/login", "/style.css"}


# ------------------------
# Log files
# ------------------------
def log_days():
    """All days that have a log file, newest first."""
    if not os.path.isdir(LOG_DIR):
        return []
    return sorted((name[:-4] for name in os.listdir(LOG_DIR)
                   if name.endswith(".txt") and DAY_RE.fullmatch(name[:-4])), reverse=True)


def day_path(day):
    return os.path.join(LOG_DIR, day + ".txt")


def kind_of(text):
    if text.startswith("Relay "):
        return "relay"
    if "Blynk" in text or "Ethernet" in text or text.startswith("Contact with controller"):
        return "network"
    return "system"


def read_day(day):
    entries = []
    with open(day_path(day), encoding="utf-8", errors="replace") as f:
        for line in f:
            m = LINE_RE.fullmatch(line.rstrip("\r\n"))
            if m:
                entries.append({"time": m[1], "text": m[2], "kind": kind_of(m[2])})
    # Events queued on the Arduino while the LAN was down are appended late
    # with their original time, so the file is not always in order
    entries.sort(key=lambda e: e["time"])
    return entries


def find_entries(days, query, kind):
    """Matching entries from the given days (newest first), newest first."""
    query = query.lower()
    for day in days:
        for e in reversed(read_day(day)):
            if kind and e["kind"] != kind:
                continue
            if query and query not in e["text"].lower():
                continue
            yield e


def zip_all_logs():
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        for day in sorted(log_days()):
            z.write(day_path(day), day + ".txt")
    return buf.getvalue()


# ------------------------
# Controller (local UDP, see handleControl() in the sketch)
# ------------------------
class DeviceError(Exception):
    pass


_device_lock = threading.Lock()  # one request at a time; the controller buffers only 2 packets
_next_id = random.randint(1, 65535)
_last_seen = None  # time.time() of the last reply
_status_cache = {"at": 0.0, "value": None}


def parse_reply(parts):
    """'R <id> <OK|ERR> <relay bits> <blynk 0|1> <uptime s> <temp> <hum>' -> dict"""
    def number(text):
        value = float(text)
        return None if math.isnan(value) else value

    bits = int(parts[3])
    return {
        "ok": parts[2] == "OK",
        "blynk": parts[4] == "1",
        "uptime": int(parts[5]),
        "temperature": number(parts[6]),
        "humidity": number(parts[7]),
        "relays": [{"n": i + 1, "name": RELAY_NAMES[i], "momentary": i < MOMENTARY_RELAYS,
                    "on": bool(bits >> i & 1)} for i in range(TOTAL_RELAYS)],
    }


def device_request(command):
    """Send one command and return the parsed reply. A lost packet is resent
    with the same id, so the controller won't run a command twice."""
    global _next_id, _last_seen
    with _device_lock:
        _next_id = _next_id % 65535 + 1
        request_id = str(_next_id)
        packet = f"C {DEVICE_KEY} {request_id} {command}".encode("ascii")

        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            for _ in range(ATTEMPTS):
                try:
                    sock.sendto(packet, (DEVICE_IP, DEVICE_PORT))
                except OSError:
                    break  # no route to the controller's network
                deadline = time.monotonic() + REPLY_TIMEOUT
                while (left := deadline - time.monotonic()) > 0:
                    sock.settimeout(left)
                    try:
                        data, addr = sock.recvfrom(128)
                    except OSError:  # timeout, or ICMP port unreachable
                        break
                    parts = data.decode("ascii", "replace").split()
                    if addr[0] != DEVICE_IP or len(parts) != 8 or parts[:2] != ["R", request_id]:
                        continue  # late reply to an earlier attempt or request
                    try:
                        reply = parse_reply(parts)
                    except ValueError:
                        continue
                    _last_seen = time.time()
                    return reply
    raise DeviceError(f"No reply from the controller at {DEVICE_IP}")


def cache_status(status):
    _status_cache.update(at=time.monotonic(), value=status)


def controller_status():
    if not DEVICE_IP:
        return {"configured": False}
    status = _status_cache["value"]
    if not status or time.monotonic() - _status_cache["at"] >= STATUS_CACHE_SECONDS:
        try:
            reply = device_request("S")
            reply.pop("ok")
            status = {"configured": True, "reachable": True, **reply}
        except DeviceError as e:
            status = {"configured": True, "reachable": False, "error": str(e)}
        cache_status(status)
    seen_ago = None if _last_seen is None else int(time.time() - _last_seen)
    return {**status, "device": DEVICE_IP, "lastSeenAgo": seen_ago}


def set_relay(n, body):
    """Pulse a momentary relay or switch a power relay. Returns an error message or None."""
    if not DEVICE_IP:
        return "DEVICE_IP is not set"
    if not 1 <= n <= TOTAL_RELAYS:
        return "No such relay"
    if n <= MOMENTARY_RELAYS:
        command = f"P {n}"
    elif isinstance(body.get("on"), bool):
        command = f"W {n} {int(body['on'])}"
    else:
        return 'Expected {"on": true|false}'

    reply = device_request(command)
    if not reply.pop("ok"):
        return "The controller rejected the command"
    cache_status({"configured": True, "reachable": True, **reply})
    return None


# ------------------------
# Login sessions (in memory: a restart logs everyone out)
# ------------------------
sessions = {}  # token -> expiry (time.time())


def login_enabled():
    return bool(WEB_USER and WEB_PASSWORD)


def check_login(user, password):
    # Compare both so a wrong username takes as long as a wrong password
    user_ok = hmac.compare_digest(user.encode(), WEB_USER.encode())
    password_ok = hmac.compare_digest(password.encode(), WEB_PASSWORD.encode())
    return user_ok and password_ok


def new_session():
    now = time.time()
    for token in [t for t, expiry in sessions.items() if expiry < now]:
        del sessions[token]
    token = secrets.token_urlsafe(32)
    sessions[token] = now + SESSION_SECONDS
    return token


# ------------------------
# HTTP
# ------------------------
class Handler(BaseHTTPRequestHandler):
    server_version = "HomeAutomation"

    def log_message(self, fmt, *args):
        pass  # the page polls; keep the container log for real problems

    def session_token(self):
        try:
            morsel = SimpleCookie(self.headers.get("Cookie", "")).get("session")
        except CookieError:
            return None
        return morsel.value if morsel else None

    def logged_in(self):
        if not login_enabled():
            return True
        expiry = sessions.get(self.session_token())
        return expiry is not None and expiry > time.time()

    def authorized(self, path):
        """True if the request may go on; otherwise sends a 401 or a redirect to the login page."""
        if path in PUBLIC_PATHS or self.logged_in():
            return True
        if path.startswith("/api/"):
            self.send_json({"error": "Not logged in"}, 401)
        else:
            self.redirect("login")
        return False

    def redirect(self, location, cookie=None):
        self.send_response(303)
        self.send_header("Location", location)
        if cookie:
            self.send_header("Set-Cookie", cookie)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def read_body(self):
        return self.rfile.read(int(self.headers.get("Content-Length", 0)))

    def send_body(self, data, content_type, status=200, filename=None):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        if filename:
            self.send_header("Content-Disposition", f'attachment; filename="{filename}"')
        self.end_headers()
        self.wfile.write(data)

    def send_json(self, value, status=200):
        self.send_body(json.dumps(value).encode(), "application/json", status)

    def do_GET(self):
        url = urlparse(self.path)
        qs = {k: v[0] for k, v in parse_qs(url.query).items()}
        path = url.path
        if not self.authorized(path):
            return

        try:
            if path == "/login":
                if self.logged_in():
                    return self.redirect("./")
                self.send_static("login.html")
            elif path in STATIC_FILES:
                self.send_static(STATIC_FILES[path])
            elif path == "/api/status":
                self.send_json({**controller_status(), "login": login_enabled()})
            elif path == "/api/days":
                self.send_json([{"day": d, "size": os.path.getsize(day_path(d))} for d in log_days()])
            elif path == "/api/entries":
                self.get_entries(qs)
            elif path == "/api/logs.zip":
                self.send_body(zip_all_logs(), "application/zip", filename="homeauto-logs.zip")
            elif (m := re.fullmatch(r"/api/logs/(\d{4}-\d{2}-\d{2})\.txt", path)) and m[1] in log_days():
                with open(day_path(m[1]), "rb") as f:
                    self.send_body(f.read(), "text/plain; charset=utf-8", filename=m[1] + ".txt")
            else:
                self.send_json({"error": "Not found"}, 404)
        except OSError as e:  # e.g. a log file deleted while being read
            self.send_json({"error": str(e)}, 500)

    def send_static(self, name):
        content_type = "text/css" if name.endswith(".css") else "text/html; charset=utf-8"
        with open(os.path.join(STATIC_DIR, name), "rb") as f:
            self.send_body(f.read(), content_type)

    def get_entries(self, qs):
        """?day=YYYY-MM-DD or ?from=&to=, plus optional q, kind, offset, limit."""
        try:
            offset = max(0, int(qs.get("offset", 0)))
            limit = min(MAX_LIMIT, max(1, int(qs.get("limit", 500))))
        except ValueError:
            return self.send_json({"error": "offset and limit must be numbers"}, 400)

        days = log_days()
        if "day" in qs:
            days = [d for d in days if d == qs["day"]]
        if "from" in qs:
            days = [d for d in days if d >= qs["from"]]
        if "to" in qs:
            days = [d for d in days if d <= qs["to"]]

        matches = list(find_entries(days, qs.get("q", ""), qs.get("kind", "")))
        self.send_json({"total": len(matches), "entries": matches[offset:offset + limit]})

    def do_POST(self):
        path = urlparse(self.path).path
        if path == "/login":
            return self.post_login()
        if path == "/logout":
            sessions.pop(self.session_token(), None)
            return self.redirect("login", "session=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax")
        if not self.authorized(path):
            return
        m = re.fullmatch(r"/api/relay/(\d+)", path)
        if not m:
            return self.send_json({"error": "Not found"}, 404)
        # Plain HTML forms can't send JSON, so this also blocks cross-site form posts
        if not self.headers.get("Content-Type", "").startswith("application/json"):
            return self.send_json({"error": "Expected application/json"}, 415)
        try:
            body = json.loads(self.read_body() or b"{}")
            if not isinstance(body, dict):
                raise ValueError
        except ValueError:
            return self.send_json({"error": "Invalid JSON"}, 400)

        try:
            error = set_relay(int(m[1]), body)
        except DeviceError as e:
            return self.send_json({"error": str(e)}, 504)
        if error:
            return self.send_json({"error": error}, 400)
        self.send_json({**controller_status(), "login": login_enabled()})

    def post_login(self):
        if not login_enabled():
            return self.redirect("./")
        form = {k: v[0] for k, v in parse_qs(self.read_body().decode("utf-8", "replace")).items()}
        if not check_login(form.get("user", ""), form.get("password", "")):
            time.sleep(1)  # slows down password guessing
            return self.redirect("login?error=1")
        cookie = f"session={new_session()}; Path=/; Max-Age={SESSION_SECONDS}; HttpOnly; SameSite=Lax"
        self.redirect("./", cookie)


def main():
    if DEVICE_IP and not re.fullmatch(r"[!-~]{1,32}", DEVICE_KEY):
        sys.exit("DEVICE_KEY must be set to LOCAL_CONTROL_KEY from secrets.h (1-32 characters, no spaces)")
    server = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    print(f"Web app on http://0.0.0.0:{PORT}, reading logs from {os.path.abspath(LOG_DIR)}", flush=True)
    if not DEVICE_IP:
        print("DEVICE_IP not set: relay control and readings are disabled", flush=True)
    elif not (WEB_USER and WEB_PASSWORD):
        print("WARNING: relay control is enabled without WEB_USER / WEB_PASSWORD", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
