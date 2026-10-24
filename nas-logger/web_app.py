#!/usr/bin/env python3
"""Home automation web app.

Serves a dashboard (relays, temperature, humidity) and a browser for the
daily log files written by nas_logger.py. Relay control and sensor readings
go through the Blynk HTTP API, so the controller firmware needs no changes.

Settings via environment variables:
  LOG_DIR       folder with the daily .txt files     (default: ./logs)
  WEB_PORT      HTTP port                            (default: 8080)
  WEB_USER      login for the web page; set both when BLYNK_TOKEN is set,
  WEB_PASSWORD  since the page can open the gate / garage
  BLYNK_TOKEN   device auth token (same as in secrets.h); without it the
                dashboard shows the logs only
  BLYNK_SERVER  Blynk server host                    (default: blynk.cloud)
  RELAY_NAMES   comma-separated names for relays 1-8 (default: Relay 1..8)
"""
import hmac
import io
import json
import os
import re
import secrets
import time
import zipfile
from http.cookies import CookieError, SimpleCookie
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlencode, urlparse
from urllib.request import urlopen

LOG_DIR = os.environ.get("LOG_DIR", "logs")
PORT = int(os.environ.get("WEB_PORT", "8080"))
WEB_USER = os.environ.get("WEB_USER", "")
WEB_PASSWORD = os.environ.get("WEB_PASSWORD", "")
BLYNK_TOKEN = os.environ.get("BLYNK_TOKEN", "")
BLYNK_SERVER = os.environ.get("BLYNK_SERVER", "blynk.cloud")
STATIC_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "static")

TOTAL_RELAYS = 8
MOMENTARY_RELAYS = 4  # relays 1-4 pulse (V0-V3), relays 5-8 toggle (V4-V7)
_names = [n.strip() for n in os.environ.get("RELAY_NAMES", "").split(",")]
RELAY_NAMES = [(_names[i] if i < len(_names) and _names[i] else f"Relay {i + 1}")
               for i in range(TOTAL_RELAYS)]

DAY_RE = re.compile(r"\d{4}-\d{2}-\d{2}")
LINE_RE = re.compile(r"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})  (.*)")
MAX_LIMIT = 2000
STATUS_CACHE_SECONDS = 10  # keeps Blynk API usage low with several viewers
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
# Blynk
# ------------------------
class BlynkError(Exception):
    pass


def blynk(endpoint, query):
    url = f"https://{BLYNK_SERVER}/external/api/{endpoint}?token={BLYNK_TOKEN}&{query}"
    try:
        with urlopen(url, timeout=5) as r:
            return r.read().decode("utf-8")
    except OSError as e:  # URLError, HTTPError and timeouts; never echo the URL (has the token)
        raise BlynkError(f"Blynk request failed: {getattr(e, 'reason', e)}") from None


_status_cache = {"at": 0.0, "value": None}


def controller_status():
    if not BLYNK_TOKEN:
        return {"blynk": False}
    if _status_cache["value"] and time.monotonic() - _status_cache["at"] < STATUS_CACHE_SECONDS:
        return _status_cache["value"]

    online = blynk("isHardwareConnected", "").strip() == "true"
    try:
        raw = json.loads(blynk("get", "V4&V5&V6&V7&V15&V16"))
        values = {k.upper(): v for k, v in raw.items()}
    except (ValueError, AttributeError):
        raise BlynkError("Unexpected reply from Blynk") from None

    def number(pin):
        try:
            return float(values[pin])
        except (KeyError, TypeError, ValueError):
            return None

    relays = []
    for i in range(TOTAL_RELAYS):
        momentary = i < MOMENTARY_RELAYS
        on = None if momentary else number(f"V{i}") == 1
        relays.append({"n": i + 1, "name": RELAY_NAMES[i], "momentary": momentary, "on": on})

    status = {"blynk": True, "online": online, "temperature": number("V15"),
              "humidity": number("V16"), "relays": relays}
    _status_cache.update(at=time.monotonic(), value=status)
    return status


def set_relay(n, body):
    """Pulse a momentary relay or switch a power relay. Returns an error message or None."""
    if not BLYNK_TOKEN:
        return "Blynk is not configured (BLYNK_TOKEN)"
    if not 1 <= n <= TOTAL_RELAYS:
        return "No such relay"
    # A write while offline would be applied on reconnect, possibly much later
    if blynk("isHardwareConnected", "").strip() != "true":
        return "Controller is offline"

    pin = f"V{n - 1}"
    if n <= MOMENTARY_RELAYS:
        # The sketch pulses on 1 and ignores 0; resetting to 0 leaves the
        # datastream ready for the next press
        blynk("update", urlencode({pin: 1}))
        blynk("update", urlencode({pin: 0}))
    else:
        if not isinstance(body.get("on"), bool):
            return 'Expected {"on": true|false}'
        blynk("update", urlencode({pin: int(body["on"])}))
    _status_cache["value"] = None
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
        except BlynkError as e:
            self.send_json({"error": str(e)}, 502)

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
        except BlynkError as e:
            return self.send_json({"error": str(e)}, 502)
        if error:
            return self.send_json({"error": error}, 409 if error == "Controller is offline" else 400)
        self.send_json({"ok": True})

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
    server = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    print(f"Web app on http://0.0.0.0:{PORT}, reading logs from {os.path.abspath(LOG_DIR)}", flush=True)
    if not BLYNK_TOKEN:
        print("BLYNK_TOKEN not set: relay control and sensor readings are disabled", flush=True)
    elif not (WEB_USER and WEB_PASSWORD):
        print("WARNING: relay control is enabled without WEB_USER / WEB_PASSWORD", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
