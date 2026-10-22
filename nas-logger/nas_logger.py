#!/usr/bin/env python3
"""Home automation event logger.

Listens for UDP events from the Arduino and appends them to one text file
per day (YYYY-MM-DD.txt). Each event is acked so the Arduino can resend
anything that was lost, e.g. while the LAN or the Ethernet module was down.

Packet format: "<bootId> <seq> <age ms> <text>"   (seq 0 = heartbeat)

Settings via environment variables:
  LOG_DIR   folder for the daily .txt files   (default: ./logs)
  LOG_PORT  UDP port                         (default: 5140)
"""
import os
import socket
from collections import deque
from datetime import datetime, timedelta

LOG_DIR = os.environ.get("LOG_DIR", "logs")
PORT = int(os.environ.get("LOG_PORT", "5140"))
SILENCE_LIMIT = timedelta(minutes=3)  # heartbeat is every 60 s


def write(ts, text):
    path = os.path.join(LOG_DIR, ts.strftime("%Y-%m-%d") + ".txt")
    with open(path, "a", encoding="utf-8") as f:
        f.write(f"{ts:%Y-%m-%d %H:%M:%S}  {text}\n")
    print(f"{ts:%Y-%m-%d %H:%M:%S}  {text}", flush=True)


def fmt_duration(delta):
    minutes, seconds = divmod(int(delta.total_seconds()), 60)
    hours, minutes = divmod(minutes, 60)
    return f"{hours}h {minutes}m {seconds}s" if hours else f"{minutes}m {seconds}s"


def main():
    os.makedirs(LOG_DIR, exist_ok=True)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", PORT))
    print(f"Listening on UDP {PORT}, writing to {os.path.abspath(LOG_DIR)}", flush=True)

    seen = deque(maxlen=256)  # (bootId, seq) already written, to skip resends
    last_contact = None

    while True:
        data, addr = sock.recvfrom(512)
        now = datetime.now()

        parts = data.decode("ascii", errors="replace").strip().split(" ", 3)
        if len(parts) < 4:
            continue
        try:
            boot, seq, age_ms = int(parts[0]), int(parts[1]), int(parts[2])
        except ValueError:
            continue
        text = parts[3]

        if last_contact and now - last_contact > SILENCE_LIMIT:
            write(now, f"Contact with controller restored after {fmt_duration(now - last_contact)} "
                       f"of silence (LAN, Ethernet module or controller was down)")
        last_contact = now

        if seq == 0:  # heartbeat
            continue

        # Always ack, even duplicates: the previous ack may have been lost
        sock.sendto(f"ACK {boot} {seq}".encode("ascii"), addr)

        key = (boot, seq)
        if key in seen:
            continue
        seen.append(key)

        write(now - timedelta(milliseconds=age_ms), text)


if __name__ == "__main__":
    main()
