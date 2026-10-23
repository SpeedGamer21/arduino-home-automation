# Arduino Home Automation

An Arduino Mega controller for 8 relays. You can switch them from wall switches, an IR remote, or the Blynk app. Local control keeps working when the internet or the network is down. Events are logged to a NAS as one text file per day.

## Features

- **8 relays (active LOW)**
  - Relays 1–4 are *momentary*. They pulse for 1 s, for things like gate or garage openers.
  - Relays 5–8 are *power* relays that toggle on and off.
- **Three ways to control them:** wall push buttons, an NEC IR remote, and the Blynk app.
- **Offline-first.** Switches and IR never wait on the network.
  - Blynk reconnects in the background, retrying less often the longer it's offline.
  - The ENC28J60 is re-initialised only after repeated failures.
- **State sync.** A relay toggled locally while offline is pushed to the app when Blynk reconnects, instead of the app switching it back.
- **DHT11 temperature and humidity** go to Blynk and to a 128×64 SSD1306 OLED. The OLED also shows Blynk status, the IP address and the DNS server.
- **Event log on a NAS** (optional). Events are sent over UDP to a small Python service that writes `YYYY-MM-DD.txt` files. Events that happen while the LAN is down are queued and delivered later with their original time.

## Hardware

| Part | Pin(s) |
|---|---|
| Arduino Mega 2560 | — |
| ENC28J60 Ethernet module | SPI (50–53) |
| Relays 1–8 | 45, 43, 41, 39, 37, 35, 33, 31 |
| Wall push buttons 1–8 (to GND, internal pull-up) | 44, 42, 40, 38, 36, 34, 32, 30 |
| IR receiver | 2 |
| DHT11 | 6 |
| SSD1306 OLED (I²C, 0x3C) | SDA 20, SCL 21 |

> Power the ENC28J60 from its own 3.3 V regulator, or use the 5 V version of the module. It draws around 150–180 mA. Running it from the Mega's 3.3 V pin can cause lockups that no code can recover from.

## Firmware setup

1. Install these libraries in the Arduino IDE:
   - EthernetENC (the ENC28J60 stack Blynk uses; UIPEthernet is not needed)
   - Blynk
   - DHT sensor library (Adafruit), plus Adafruit Unified Sensor
   - Adafruit GFX and Adafruit SSD1306
   - Time
   - IRremote (4.x)
2. Copy `HomeAutomation/secrets.example.h` to `HomeAutomation/secrets.h` and fill in your Blynk template ID, template name and auth token. `secrets.h` is git-ignored.
3. Near the top of `HomeAutomation.ino`, choose the network mode:
   - `USE_STATIC_IP 1` (recommended): set `staticIP`, `dnsIP`, `gatewayIP` and `subnetMask`. Use an address outside your router's DHCP pool. Re-initialising Ethernet never blocks in this mode.
   - `USE_STATIC_IP 0`: use DHCP. Re-initialising Ethernet can block for up to 10 s (`dhcpTimeout`) while the router itself is down.
4. Set `nasIP` to your NAS's address if you use the logger.
5. If your remote is different, replace the codes in `irCodes[]`. Entries 0–7 map to relays 1–8.

### Blynk virtual pins

| Pin | Function |
|---|---|
| V0–V3 | Pulse relays 1–4 (push button) |
| V4–V7 | Relays 5–8 on/off (switch) |
| V15 | Temperature (°C) |
| V16 | Humidity (%) |

## NAS event logger

`nas-logger/nas_logger.py` uses only the Python 3 standard library.

- It listens on UDP port 5140 and acknowledges each event, so the Arduino can resend anything lost.
- It appends each event to a text file for that day.
- If the Arduino's 60-second heartbeat goes quiet for more than 3 minutes, it writes a line noting the gap.

Run it directly:

```sh
LOG_DIR=/volume1/homeauto-logs python3 nas_logger.py
```

Or run it with Docker:

```sh
docker run -d --name homeauto-logger --restart unless-stopped -p 5140:5140/udp \
  -v /volume1/homeauto-logs:/logs -v /path/to/nas-logger:/app \
  -v /etc/localtime:/etc/localtime:ro -e LOG_DIR=/logs \
  python:3.12-alpine python -u /app/nas_logger.py
```

If the NAS has a firewall, allow UDP port 5140 from the controller.

Example log:

```
2026-10-22 22:10:56  Controller started
2026-10-22 22:11:40  Relay 5 switched ON by IR remote
2026-10-22 22:12:03  Relay 2 pulsed by app
2026-10-22 23:40:12  Blynk OFFLINE (internet or Blynk server unreachable)
2026-10-22 23:42:30  Ethernet module re-initialised after 6 failed reconnects
2026-10-22 23:55:01  Blynk ONLINE (was offline 889 s)
```

> The logger protocol has no authentication. Only expose it on a trusted LAN.
