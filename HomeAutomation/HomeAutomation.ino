// Blynk credentials live in secrets.h (not committed). Copy secrets.example.h
// to secrets.h and fill in your own values.
#include "secrets.h"
#define BLYNK_PRINT Serial

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define TEMP_S V15
#define HUM_S  V16
#define DHTPIN 6
#define DHTTYPE DHT11

// Network mode
// 0 = DHCP (router assigns the IP). An Ethernet re-init can block up to
//     dhcpTimeout (10 s) while the router itself is down, freezing local controls.
// 1 = Static IP (recommended). Ethernet.begin() returns immediately, so local
//     controls never freeze. Set the addresses below first.
#define USE_STATIC_IP 0

#include <EthernetENC.h>  // ENC28J60 (the stack BlynkSimpleEthernetENC.h is built on)
#include <BlynkSimpleEthernetENC.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <TimeLib.h>
#include <IRremote.hpp>  // IRremote 4.x

#if USE_STATIC_IP
IPAddress staticIP(192, 168, 1, 50);     // pick an address outside the router's DHCP pool
IPAddress dnsIP(192, 168, 1, 1);
IPAddress gatewayIP(192, 168, 1, 1);
IPAddress subnetMask(255, 255, 255, 0);
#endif

// Event log -> NAS (runs nas_logger.py, which writes one .txt file per day)
IPAddress nasIP(192, 168, 1, 10);   // your NAS address
const unsigned int logPort = 5140;  // UDP port, same on both sides

// Relay pins
const int relayPins[]  = {45, 43, 41, 39, 37, 35, 33, 31};
const int switchPins[] = {44, 42, 40, 38, 36, 34, 32, 30};

const int irReceiverPin = 2;
const int numMomentaryRelays = 4;
const int numPowerRelays = 4;
const int totalRelays = 8;

// IR codes
const unsigned long irCodes[] = {
  0xBA45FF00,
  0xB946FF00,
  0xB847FF00,
  0xBB44FF00,
  0xBF40FF00,
  0xBC43FF00,
  0xF807FF00,
  0xEA15FF00
};

DHT dht(DHTPIN, DHTTYPE);

#define OLED_RESET 4
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Your logo bitmap stays unchanged
static const unsigned char PROGMEM image_data_LOGOarray[] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0xff, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xff, 0xff, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0xff, 0xff, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xff, 0xff, 0xff, 0xe7, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xff, 0xff, 0xff, 0xc7, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xff, 0xff, 0xff, 0x07, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0xff, 0xff, 0xf0, 0x07, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0xff, 0xf4, 0x00, 0x0f, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xff, 0x00, 0x00, 0x0f, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xf8, 0x00, 0x00, 0x1f, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xe0, 0x00, 0x00, 0x1f, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xc0, 0x00, 0x00, 0x3f, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x80, 0x00, 0x00, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0x17, 0x80, 0x4b, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x01, 0xfe, 0x7f, 0x81, 0xff, 0xff, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x01, 0xf8, 0xff, 0x01, 0xff, 0xff, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x01, 0xf9, 0xff, 0x01, 0xff, 0xff, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x01, 0xf1, 0xff, 0x03, 0xff, 0x8e, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x01, 0xf3, 0xff, 0x03, 0xff, 0x06, 0x7f, 0xc0, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x03, 0xe3, 0xfe, 0x07, 0xff, 0x00, 0x7f, 0xc0, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x01, 0xe3, 0xfe, 0x07, 0xff, 0x00, 0x7f, 0xe0, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x03, 0xe3, 0xfe, 0x0f, 0xff, 0x80, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x01, 0xe3, 0xfc, 0x0f, 0x07, 0xc3, 0xff, 0xe0, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x03, 0xe1, 0xfc, 0x0e, 0x03, 0xc3, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x01, 0xe0, 0xfc, 0x1e, 0x03, 0xc1, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x01, 0xe0, 0x3c, 0x1c, 0x03, 0xc3, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x01, 0xe0, 0x7c, 0x1c, 0x63, 0xc3, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x01, 0xf0, 0xf8, 0x1c, 0x63, 0xc3, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x01, 0xfb, 0xfc, 0x1c, 0x63, 0xc3, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xf8, 0x3c, 0x43, 0xc7, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xfc, 0x1c, 0x47, 0x87, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xfc, 0x1c, 0x07, 0x0f, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xfc, 0x1e, 0x0f, 0x0f, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xfe, 0x0e, 0x1c, 0x1f, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xfe, 0x04, 0x00, 0x3f, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xff, 0x00, 0x00, 0x3f, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0xff, 0x81, 0xe0, 0xff, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xff, 0xff, 0xff, 0xff, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xff, 0xff, 0xff, 0xff, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xff, 0xff, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xff, 0xff, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xff, 0xff, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
char auth[] = BLYNK_AUTH_TOKEN;

// Debounce
const unsigned long debounceTime = 200;   // repeat guard for IR / Blynk triggers
const unsigned long switchDebounce = 50;  // contact bounce filter for wall switches
unsigned long lastPressTime[8] = {0};

// Wall switch edge detection (power relays toggle once per press, not while held)
int switchRaw[8];
int switchStable[8];
unsigned long switchChangeTime[8] = {0};

// Momentary relay state
bool momentaryActive[4] = {false, false, false, false};
unsigned long momentaryStart[4] = {0, 0, 0, 0};
const unsigned long momentaryDuration = 1000;

// Power relay changed locally (IR / wall switch) while offline -> push to app on reconnect
bool pendingAppUpdate[4] = {false, false, false, false};

// Reconnect control (interval grows while offline so blocking attempts happen less often)
unsigned long lastReconnectAttempt = 0;
unsigned long reconnectInterval = 5000;
const unsigned long reconnectIntervalMin = 5000;
const unsigned long reconnectIntervalMax = 30000;
uint8_t reconnectFailures = 0;
const uint8_t failuresBeforeEthernetReset = 6;
const unsigned long dhcpTimeout = 10000;      // library default is 60 s
const uint16_t blynkConnectTimeout = 3000;    // TCP connect, library default is 5 s

// Display timing (redraw only when something changed)
unsigned long lastDisplayUpdate = 0;
const unsigned long displayInterval = 2000;
bool displayDirty = true;
bool lastShownOnline = false;

// Sensor timing / cached readings
unsigned long lastSensorRead = 0;
const unsigned long sensorInterval = 10000;
float temperature = NAN;
float humidity = NAN;

// Online / offline tracking for the log
bool everOnline = false;
unsigned long offlineSince = 0;

// ------------------------
// Event log
// ------------------------
// Events are queued in RAM and sent one at a time to the NAS, which acks each
// one. Unacked events are resent, so events that happen while the LAN or the
// ENC28J60 is down still arrive (with their original time) once it's back.
enum EventType : uint8_t { EV_BOOT, EV_ONLINE, EV_OFFLINE, EV_ETH_REINIT, EV_MOMENTARY, EV_POWER, EV_DROPPED };
enum EventSource : uint8_t { SRC_APP, SRC_IR, SRC_SWITCH };

struct LogEvent {
  uint32_t ms;     // millis() when it happened
  uint32_t value;
  uint16_t seq;
  uint8_t type;
  uint8_t a;
  uint8_t b;
};

EthernetUDP logUdp;
const uint8_t logQueueSize = 24;
LogEvent logQueue[logQueueSize];
uint8_t logHead = 0;
uint8_t logCount = 0;
uint16_t logSeq = 0;
uint16_t bootId = 0;           // lets the NAS tell reboots apart
uint32_t logDropped = 0;
bool logAwaitingAck = false;
unsigned long lastLogSend = 0;
const unsigned long logRetryInterval = 2000;
unsigned long lastHeartbeat = 0;
const unsigned long heartbeatInterval = 60000;

// ------------------------
// Relay helpers
// ------------------------
bool isRelayOn(int relayIndex) {
  return digitalRead(relayPins[relayIndex]) == LOW; // active LOW
}

void triggerMomentaryRelay(int relayIndex, uint8_t source) {
  unsigned long currentMillis = millis();

  if (currentMillis - lastPressTime[relayIndex] >= debounceTime) {
    lastPressTime[relayIndex] = currentMillis;

    // Log once per pulse, not on every re-trigger while a button is held
    if (!momentaryActive[relayIndex]) logEvent(EV_MOMENTARY, relayIndex, source, 0);

    digitalWrite(relayPins[relayIndex], LOW);  // ON (active LOW)
    momentaryStart[relayIndex] = currentMillis;
    momentaryActive[relayIndex] = true;
  }
}

void updateMomentaryRelays() {
  unsigned long currentMillis = millis();

  for (int i = 0; i < numMomentaryRelays; i++) {
    if (momentaryActive[i] && (currentMillis - momentaryStart[i] >= momentaryDuration)) {
      digitalWrite(relayPins[i], HIGH);  // OFF
      momentaryActive[i] = false;
    }
  }
}

// Keep the app button in step with a local change, or remember it for the next reconnect
void reportPowerRelay(int relayIndex) {
  int i = relayIndex - numMomentaryRelays;

  if (Blynk.connected()) {
    Blynk.virtualWrite(V4 + i, isRelayOn(relayIndex) ? 1 : 0);
    pendingAppUpdate[i] = false;
  } else {
    pendingAppUpdate[i] = true;
  }
}

void togglePowerRelay(int relayIndex, uint8_t source) {
  digitalWrite(relayPins[relayIndex], isRelayOn(relayIndex) ? HIGH : LOW);
  reportPowerRelay(relayIndex);
  logEvent(EV_POWER, relayIndex, source, isRelayOn(relayIndex));
}

void setPowerRelayFromApp(int i, int value) {
  int relayIndex = i + numMomentaryRelays;
  bool on = value != 0;

  // Only log real changes (syncVirtual on every reconnect re-sends the same state)
  if (on != isRelayOn(relayIndex)) logEvent(EV_POWER, relayIndex, SRC_APP, on);

  digitalWrite(relayPins[relayIndex], on ? LOW : HIGH);
  pendingAppUpdate[i] = false;
}

// ------------------------
// Blynk handlers
// ------------------------
BLYNK_WRITE(V0) { if (param.asInt()) triggerMomentaryRelay(0, SRC_APP); }
BLYNK_WRITE(V1) { if (param.asInt()) triggerMomentaryRelay(1, SRC_APP); }
BLYNK_WRITE(V2) { if (param.asInt()) triggerMomentaryRelay(2, SRC_APP); }
BLYNK_WRITE(V3) { if (param.asInt()) triggerMomentaryRelay(3, SRC_APP); }

BLYNK_WRITE(V4) { setPowerRelayFromApp(0, param.asInt()); }
BLYNK_WRITE(V5) { setPowerRelayFromApp(1, param.asInt()); }
BLYNK_WRITE(V6) { setPowerRelayFromApp(2, param.asInt()); }
BLYNK_WRITE(V7) { setPowerRelayFromApp(3, param.asInt()); }

BLYNK_CONNECTED() {
  for (int i = 0; i < numPowerRelays; i++) {
    if (pendingAppUpdate[i]) {
      // Changed locally while offline: local state wins, push it to the app
      Blynk.virtualWrite(V4 + i, isRelayOn(i + numMomentaryRelays) ? 1 : 0);
      pendingAppUpdate[i] = false;
    } else {
      // No local change: restore the state stored in the app
      Blynk.syncVirtual(V4 + i);
    }
  }
}

// ------------------------
// Inputs
// ------------------------
void checkManualSwitches() {
  unsigned long now = millis();

  for (int i = 0; i < totalRelays; i++) {
    int raw = digitalRead(switchPins[i]);

    if (raw != switchRaw[i]) {
      switchRaw[i] = raw;
      switchChangeTime[i] = now;
    }

    if (now - switchChangeTime[i] >= switchDebounce && raw != switchStable[i]) {
      switchStable[i] = raw;

      // Power relays toggle once per press
      if (raw == LOW && i >= numMomentaryRelays) {
        togglePowerRelay(i, SRC_SWITCH);
      }
    }

    // Momentary relays stay energised while the button is held (same as before)
    if (i < numMomentaryRelays && switchStable[i] == LOW) {
      triggerMomentaryRelay(i, SRC_SWITCH);
    }
  }
}

void checkIRRemote() {
  if (IrReceiver.decode()) {
    unsigned long receivedCode = IrReceiver.decodedIRData.decodedRawData;
    IrReceiver.resume();

    for (int i = 0; i < numMomentaryRelays; i++) {
      if (receivedCode == irCodes[i]) {
        Serial.println(F("IR triggered momentary relay"));
        triggerMomentaryRelay(i, SRC_IR);
        return;
      }
    }

    for (int i = 0; i < numPowerRelays; i++) {
      int relayIndex = i + numMomentaryRelays;

      if (receivedCode == irCodes[relayIndex]) {
        Serial.println(F("IR toggled power relay"));

        unsigned long currentMillis = millis();
        if (currentMillis - lastPressTime[relayIndex] >= debounceTime) {
          lastPressTime[relayIndex] = currentMillis;
          togglePowerRelay(relayIndex, SRC_IR);
        }
        return;
      }
    }
  }
}

// ------------------------
// Blynk / Ethernet
// ------------------------
bool startEthernet() {
  logUdp.stop();

#if USE_STATIC_IP
  Ethernet.begin(mac, staticIP, dnsIP, gatewayIP, subnetMask);  // returns immediately
  bool ok = true;
#else
  bool ok = Ethernet.begin(mac, dhcpTimeout) != 0;  // DHCP, blocks until a lease or timeout
#endif

  logUdp.begin(logPort);  // re-init wipes the UDP socket, so reopen it
  return ok;
}

void tryBlynkReconnect() {
  unsigned long now = millis();

  if (now - lastReconnectAttempt < reconnectInterval) return;

  Serial.println(F("Trying Blynk reconnect..."));
  Blynk.disconnect();  // stop the old client so UIPEthernet frees its socket

  if (Blynk.connect(300)) {
    Serial.println(F("Blynk reconnected"));
    reconnectFailures = 0;
    reconnectInterval = reconnectIntervalMin;
  } else {
    reconnectFailures++;
    if (reconnectInterval < reconnectIntervalMax) reconnectInterval += 5000;

    // Re-init the ENC28J60 only after repeated failures (recovers a locked-up
    // chip, or picks up a new lease after a router reboot)
    if (reconnectFailures >= failuresBeforeEthernetReset) {
      reconnectFailures = 0;
      Serial.println(F("Re-initialising Ethernet..."));
      Blynk.disconnect();
      bool ok = startEthernet();
      if (!ok) Serial.println(F("DHCP failed"));
      logEvent(EV_ETH_REINIT, ok, 0, failuresBeforeEthernetReset);
      displayDirty = true;
    }
  }

  // Measure the interval from the end of the attempt, since connecting can block
  lastReconnectAttempt = millis();
}

// ------------------------
// Event log
// ------------------------
void logEvent(uint8_t type, uint8_t a, uint8_t b, uint32_t value) {
  if (logCount == logQueueSize) {  // full: drop the oldest
    logHead = (logHead + 1) % logQueueSize;
    logCount--;
    logDropped++;
    logAwaitingAck = false;
  }

  LogEvent &e = logQueue[(logHead + logCount) % logQueueSize];
  e.ms = millis();
  e.value = value;
  e.seq = ++logSeq;
  if (e.seq == 0) e.seq = ++logSeq;  // 0 is reserved for heartbeats
  e.type = type;
  e.a = a;
  e.b = b;
  logCount++;
}

void printSource(uint8_t source) {
  if (source == SRC_APP) logUdp.print(F("app"));
  else if (source == SRC_IR) logUdp.print(F("IR remote"));
  else logUdp.print(F("wall switch"));
}

void printEvent(const LogEvent &e) {
  switch (e.type) {
    case EV_BOOT:
      logUdp.print(F("Controller started"));
      if (!e.a) logUdp.print(F(" (DHCP failed, running offline)"));
      break;
    case EV_ONLINE:
      logUdp.print(F("Blynk ONLINE"));
      if (e.value) {
        logUdp.print(F(" (was offline "));
        logUdp.print(e.value);
        logUdp.print(F(" s)"));
      }
      break;
    case EV_OFFLINE:
      logUdp.print(F("Blynk OFFLINE (internet or Blynk server unreachable)"));
      break;
    case EV_ETH_REINIT:
      logUdp.print(F("Ethernet module re-initialised after "));
      logUdp.print(e.value);
      logUdp.print(F(" failed reconnects"));
      if (!e.a) logUdp.print(F(", DHCP FAILED"));
      break;
    case EV_MOMENTARY:
      logUdp.print(F("Relay "));
      logUdp.print(e.a + 1);
      logUdp.print(F(" pulsed by "));
      printSource(e.b);
      break;
    case EV_POWER:
      logUdp.print(F("Relay "));
      logUdp.print(e.a + 1);
      logUdp.print(e.value ? F(" switched ON by ") : F(" switched OFF by "));
      printSource(e.b);
      break;
    case EV_DROPPED:
      logUdp.print(e.value);
      logUdp.print(F(" events lost (log queue full while NAS unreachable)"));
      break;
  }
}

// Non-blocking: handles at most one ack and one send per call
void processLog() {
  // Ack from the NAS: "ACK <bootId> <seq>"
  if (logUdp.parsePacket() > 0) {
    char buf[24];
    int n = logUdp.read((unsigned char*)buf, sizeof(buf) - 1);
    buf[n > 0 ? n : 0] = 0;
    logUdp.discardReceived();

    unsigned int ackBoot, ackSeq;
    if (sscanf(buf, "ACK %u %u", &ackBoot, &ackSeq) == 2 &&
        logCount > 0 && ackBoot == bootId && ackSeq == logQueue[logHead].seq) {
      logHead = (logHead + 1) % logQueueSize;
      logCount--;
      logAwaitingAck = false;
    }
  }

  if (logDropped && logCount < logQueueSize) {
    uint32_t dropped = logDropped;
    logDropped = 0;
    logEvent(EV_DROPPED, 0, 0, dropped);
  }

  unsigned long now = millis();

  if (logCount > 0) {
    if (logAwaitingAck && now - lastLogSend < logRetryInterval) return;

    // "<bootId> <seq> <age ms> <text>" - the NAS subtracts the age to get the real time
    const LogEvent &e = logQueue[logHead];
    if (logUdp.beginPacket(nasIP, logPort)) {
      logUdp.print(bootId);
      logUdp.print(' ');
      logUdp.print(e.seq);
      logUdp.print(' ');
      logUdp.print(now - e.ms);
      logUdp.print(' ');
      printEvent(e);
      logUdp.endPacket();
    }
    lastLogSend = now;
    logAwaitingAck = true;
  } else if (now - lastHeartbeat >= heartbeatInterval) {
    // Heartbeat (seq 0, not written to the file) lets the NAS notice silent gaps
    lastHeartbeat = now;
    if (logUdp.beginPacket(nasIP, logPort)) {
      logUdp.print(bootId);
      logUdp.print(F(" 0 0 HB"));
      logUdp.endPacket();
    }
  }
}

// ------------------------
// Sensors / Display
// ------------------------
void readSensor() {
  // One real DHT read per call: readTemperature() reuses the reading
  // readHumidity() just took (library caches it for 2 s)
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (isnan(h) || isnan(t)) {
    Serial.println(F("Failed to read from DHT sensor!"));
    return;
  }

  if (h != humidity || t != temperature) {
    humidity = h;
    temperature = t;
    displayDirty = true;
  }

  if (Blynk.connected()) {
    Blynk.virtualWrite(TEMP_S, t);
    Blynk.virtualWrite(HUM_S, h);
  }
}

void lcdText() {
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);

  display.setCursor(2, 0);
  display.print(F("Blynk:"));
  display.setCursor(60, 0);
  display.print(Blynk.connected() ? F("ONLINE") : F("OFFLINE"));

  display.setCursor(2, 15);
  display.print(F("Humidity:"));
  display.setCursor(80, 15);
  if (isnan(humidity)) display.print(F("--"));
  else display.print(humidity);
  display.print(F("%"));

  display.setCursor(2, 30);
  display.print(F("Temperature:"));
  display.setCursor(80, 30);
  if (isnan(temperature)) display.print(F("--"));
  else display.print(temperature);
  display.print((char)247);
  display.print(F("C"));

  display.setCursor(2, 45);
  display.print(F("DNS:"));
  display.setCursor(60, 45);
  display.print(Ethernet.dnsServerIP());

  display.setCursor(2, 55);
  display.print(F("IP Add:"));
  display.setCursor(60, 55);
  display.print(Ethernet.localIP());

  display.display();
}

// ------------------------
// Setup / Loop
// ------------------------
void setup() {
  // Relays first so they are held OFF during the boot logo
  for (int i = 0; i < totalRelays; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], HIGH); // OFF, active LOW
  }

  // Switch init
  for (int i = 0; i < totalRelays; i++) {
    pinMode(switchPins[i], INPUT_PULLUP);
    switchRaw[i] = digitalRead(switchPins[i]);
    switchStable[i] = switchRaw[i];
  }

  Serial.begin(9600);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  Wire.setClock(400000); // faster I2C so display refreshes block the loop less

  display.clearDisplay();
  display.drawBitmap(0, 0, image_data_LOGOarray, 128, 64, 1);
  display.display();
  delay(2000);

  dht.begin();
  IrReceiver.begin(irReceiverPin, DISABLE_LED_FEEDBACK);

  randomSeed(analogRead(A15) ^ micros());  // A15 left unconnected
  bootId = random(1, 65535);

  // Start Ethernet + Blynk config once (no Blynk.begin, so offline mode never blocks)
  bool ethernetOk = startEthernet();
  if (!ethernetOk) Serial.println(F("DHCP failed, running offline"));
  logEvent(EV_BOOT, ethernetOk, 0, 0);
  Blynk.config(auth);
  _blynkEthernetClient.setConnectionTimeout(blynkConnectTimeout);  // client object from BlynkSimpleEthernetENC.h
  Blynk.connect(1000); // one short startup attempt
  lastReconnectAttempt = millis();

  readSensor();
}

void loop() {
  // Local controls always work first
  checkManualSwitches();
  checkIRRemote();
  updateMomentaryRelays();

  // Keep network side lightweight
  if (Blynk.connected()) {
    Blynk.run();
    Ethernet.maintain();
  } else {
    tryBlynkReconnect();
  }

  bool online = Blynk.connected();
  if (online != lastShownOnline) {
    lastShownOnline = online;
    displayDirty = true;

    if (online) {
      logEvent(EV_ONLINE, 0, 0, everOnline ? (millis() - offlineSince) / 1000 : 0);
      everOnline = true;
    } else {
      offlineSince = millis();
      logEvent(EV_OFFLINE, 0, 0, 0);
    }
  }

  processLog();

  // DHT read briefly disables interrupts, which corrupts an IR frame being
  // received, so only read while the IR receiver is idle
  if (millis() - lastSensorRead >= sensorInterval && IrReceiver.isIdle()) {
    lastSensorRead = millis();
    readSensor();
  }

  // Non-blocking display refresh, only when something changed
  if (displayDirty && millis() - lastDisplayUpdate >= displayInterval) {
    lastDisplayUpdate = millis();
    displayDirty = false;
    lcdText();
  }
}
