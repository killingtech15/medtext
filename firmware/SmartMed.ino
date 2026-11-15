/*
  SmartMed / medtext - ESP32 daily medicine reminder
  Local alarm operation: DS3231 + ESP32 NVS + buzzer.
  Remote controls and push notifications: Blynk IoT over Wi-Fi.

  IMPORTANT: Configure the Blynk template/device and Wi-Fi credentials below.
  Time Input widgets must use daily local time, with start/stop, weekday,
  sunrise/sunset and timezone selection disabled.
*/
#define BLYNK_TEMPLATE_ID   "TMPLxxxxxx"
#define BLYNK_TEMPLATE_NAME "SmartMed"
#define BLYNK_AUTH_TOKEN    "PASTE_DEVICE_AUTH_TOKEN"
#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <RTClib.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <time.h>

static const char WIFI_SSID[] = "YOUR_WIFI_NAME";
static const char WIFI_PASS[] = "YOUR_WIFI_PASSWORD";

constexpr uint8_t PIN_BUZZER = 25;
constexpr uint8_t PIN_STOP   = 27;
constexpr uint8_t PIN_SDA    = 21;
constexpr uint8_t PIN_SCL    = 22;
constexpr uint8_t OLED_ADDR  = 0x3C;
constexpr uint32_t SNOOZE_SECONDS = 300;
constexpr uint32_t NTP_RETRY_MS = 60000UL;
constexpr uint32_t NTP_RESYNC_MS = 24UL * 60UL * 60UL * 1000UL;
constexpr uint32_t CLOUD_RETRY_MS = 10000UL;
constexpr uint32_t STATUS_UPDATE_MS = 15000UL;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 35UL;
constexpr uint32_t DUE_GRACE_SECONDS = 90UL;

RTC_DS3231 rtc;
Adafruit_SSD1306 display(128, 64, &Wire, -1);
Preferences prefs;

struct MedicineSlot {
  uint32_t secondsAfterMidnight = 0;
  bool enabled = false;
  uint32_t lastTriggeredDay = 0;
};
MedicineSlot slots[4];

bool rtcReady = false;
bool displayReady = false;
bool prefsReady = false;
bool alarmActive = false;
bool snoozePending = false;
uint8_t alarmMask = 0;
uint32_t snoozeDueEpoch = 0;
uint32_t lastCloudAttempt = 0;
uint32_t lastNtpAttempt = 0;
uint32_t lastNtpSync = 0;
uint32_t lastStatusUpdate = 0;
bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
uint32_t lastButtonChange = 0;

static String twoDigits(uint8_t n) { return n < 10 ? "0" + String(n) : String(n); }
static uint32_t dayKey(const DateTime& dt) {
  return (uint32_t)dt.year() * 10000UL + (uint32_t)dt.month() * 100UL + dt.day();
}
static String timeText(uint32_t seconds) {
  if (seconds >= 86400UL) return "--:--";
  return twoDigits(seconds / 3600UL) + ":" + twoDigits((seconds / 60UL) % 60UL);
}
static String maskText(uint8_t mask) {
  String s;
  for (uint8_t i = 0; i < 4; ++i) {
    if (mask & (1U << i)) {
      if (s.length()) s += ",";
      s += "M" + String(i + 1);
    }
  }
  return s.length() ? s : "None";
}

void saveSlot(uint8_t i) {
  if (!prefsReady || i >= 4) return;
  char timeKey[4], enableKey[4], dayKeyName[4];
  snprintf(timeKey, sizeof(timeKey), "t%u", (unsigned)i);
  snprintf(enableKey, sizeof(enableKey), "e%u", (unsigned)i);
  snprintf(dayKeyName, sizeof(dayKeyName), "d%u", (unsigned)i);
  prefs.putUInt(timeKey, slots[i].secondsAfterMidnight);
  prefs.putBool(enableKey, slots[i].enabled);
  prefs.putUInt(dayKeyName, slots[i].lastTriggeredDay);
}

void saveAlarmState() {
  if (!prefsReady) return;
  prefs.putUChar("alarmMask", alarmMask);
  prefs.putBool("snoozed", snoozePending);
  prefs.putUInt("snoozeDue", snoozeDueEpoch);
}

void loadPersistentState() {
  if (!prefsReady) return;
  for (uint8_t i = 0; i < 4; ++i) {
    char timeKey[4], enableKey[4], dayKeyName[4];
    snprintf(timeKey, sizeof(timeKey), "t%u", (unsigned)i);
    snprintf(enableKey, sizeof(enableKey), "e%u", (unsigned)i);
    snprintf(dayKeyName, sizeof(dayKeyName), "d%u", (unsigned)i);
    slots[i].secondsAfterMidnight = prefs.getUInt(timeKey, 0);
    slots[i].enabled = prefs.getBool(enableKey, false);
    slots[i].lastTriggeredDay = prefs.getUInt(dayKeyName, 0);
    if (slots[i].secondsAfterMidnight >= 86400UL) {
      slots[i].secondsAfterMidnight = 0;
      slots[i].enabled = false;
      saveSlot(i);
    }
  }
  alarmMask = prefs.getUChar("alarmMask", 0) & 0x0F;
  snoozePending = prefs.getBool("snoozed", false);
  snoozeDueEpoch = prefs.getUInt("snoozeDue", 0);
  alarmActive = alarmMask != 0 && !snoozePending;
}

void drawDisplay() {
  if (!displayReady) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("SMARTMED");
  if (!rtcReady) {
    display.setCursor(0, 22);
    display.println("Clock unavailable");
    display.println("Connect Wi-Fi to set time");
    display.display();
    return;
  }
  DateTime now = rtc.now();
  display.setTextSize(2);
  display.setCursor(0, 14);
  display.print(twoDigits(now.hour())); display.print(":");
  display.print(twoDigits(now.minute())); display.print(":");
  display.print(twoDigits(now.second()));
  display.setTextSize(1);
  display.setCursor(0, 42);
  if (alarmActive) {
    display.print("ALARM: "); display.println(maskText(alarmMask));
  } else if (snoozePending) {
    display.println("SNOOZED - 5 MIN");
  } else {
    uint32_t nowSec = now.hour() * 3600UL + now.minute() * 60UL + now.second();
    int best = -1;
    uint32_t bestDelta = 86401UL;
    for (uint8_t i = 0; i < 4; ++i) {
      if (!slots[i].enabled) continue;
      uint32_t target = slots[i].secondsAfterMidnight;
      uint32_t delta = target > nowSec ? target - nowSec : 86400UL - nowSec + target;
      if (delta < bestDelta) { bestDelta = delta; best = i; }
    }
    if (best >= 0) { display.print("Next M"); display.print(best + 1); display.print(" "); display.println(timeText(slots[best].secondsAfterMidnight)); }
    else display.println("No reminders enabled");
  }
  display.display();
}

void publishStatus() {
  if (!Blynk.connected()) return;
  Blynk.virtualWrite(V10, alarmActive ? "ALARM: " + maskText(alarmMask) : (snoozePending ? "SNOOZED" : "Ready"));
  if (!rtcReady) {
    Blynk.virtualWrite(V11, "Clock unavailable");
    Blynk.virtualWrite(V12, "--:--");
    return;
  }
  DateTime now = rtc.now();
  Blynk.virtualWrite(V12, twoDigits(now.hour()) + ":" + twoDigits(now.minute()));
  uint32_t nowSec = now.hour() * 3600UL + now.minute() * 60UL + now.second();
  int best = -1;
  uint32_t bestDelta = 86401UL;
  for (uint8_t i = 0; i < 4; ++i) {
    if (!slots[i].enabled) continue;
    uint32_t target = slots[i].secondsAfterMidnight;
    uint32_t delta = target > nowSec ? target - nowSec : 86400UL - nowSec + target;
    if (delta < bestDelta) { bestDelta = delta; best = i; }
  }
  Blynk.virtualWrite(V11, best >= 0 ? "M" + String(best + 1) + " " + timeText(slots[best].secondsAfterMidnight) : "None");
}

void setBuzzer(bool on) {
  digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
}

void triggerAlarmForMask(uint8_t newMask) {
  newMask &= 0x0F;
  if (!newMask) return;
  alarmMask |= newMask;
  alarmActive = true;
  snoozePending = false;
  snoozeDueEpoch = 0;
  setBuzzer(true);
  saveAlarmState();
  if (Blynk.connected()) {
    for (uint8_t i = 0; i < 4; ++i) {
      if (newMask & (1U << i)) Blynk.logEvent("medicine_due", "Medicine reminder " + String(i + 1) + " is due");
    }
  }
  drawDisplay();
  publishStatus();
}

void stopAlarm() {
  alarmActive = false;
  snoozePending = false;
  alarmMask = 0;
  snoozeDueEpoch = 0;
  setBuzzer(false);
  saveAlarmState();
  drawDisplay();
  publishStatus();
}

void snoozeAlarm() {
  if (!alarmActive || !rtcReady || alarmMask == 0) return;
  snoozeDueEpoch = rtc.now().unixtime() + SNOOZE_SECONDS;
  snoozePending = true;
  alarmActive = false;
  setBuzzer(false);
  saveAlarmState();
  drawDisplay();
  publishStatus();
}

void checkScheduledReminders() {
  if (!rtcReady) return;
  DateTime now = rtc.now();
  const uint32_t today = dayKey(now);
  const uint32_t nowSec = now.hour() * 3600UL + now.minute() * 60UL + now.second();
  uint8_t dueMask = 0;
  for (uint8_t i = 0; i < 4; ++i) {
    if (!slots[i].enabled || slots[i].lastTriggeredDay == today) continue;
    const uint32_t dueSec = slots[i].secondsAfterMidnight;
    if (nowSec >= dueSec && nowSec <= dueSec + DUE_GRACE_SECONDS) {
      slots[i].lastTriggeredDay = today;
      saveSlot(i);
      dueMask |= (1U << i);
    }
  }
  if (dueMask) triggerAlarmForMask(dueMask);
}

void checkSnooze() {
  if (!rtcReady || !snoozePending || snoozeDueEpoch == 0) return;
  if (rtc.now().unixtime() >= snoozeDueEpoch) {
    snoozePending = false;
    alarmActive = alarmMask != 0;
    snoozeDueEpoch = 0;
    setBuzzer(alarmActive);
    saveAlarmState();
    if (alarmActive && Blynk.connected()) Blynk.logEvent("medicine_due", "Snoozed reminder is ringing again");
    drawDisplay();
    publishStatus();
  }
}

bool syncRtcFromNtp() {
  if (WiFi.status() != WL_CONNECTED) return false;
  configTzTime("IST-5:30", "pool.ntp.org", "time.nist.gov", "time.google.com");
  struct tm ti;
  if (!getLocalTime(&ti, 5000)) return false;
  if (!rtc.begin()) return false;
  rtc.adjust(DateTime(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, ti.tm_hour, ti.tm_min, ti.tm_sec));
  rtcReady = true;
  lastNtpSync = millis();
  if (alarmMask != 0) {
    if (snoozePending && snoozeDueEpoch > rtc.now().unixtime()) {
      alarmActive = false;
      setBuzzer(false);
    } else {
      snoozePending = false;
      snoozeDueEpoch = 0;
      alarmActive = true;
      setBuzzer(true);
      saveAlarmState();
    }
  }
  drawDisplay();
  return true;
}

void serviceNetwork() {
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastCloudAttempt >= CLOUD_RETRY_MS) {
      lastCloudAttempt = millis();
      WiFi.reconnect();
    }
    return;
  }
  if (!Blynk.connected() && millis() - lastCloudAttempt >= CLOUD_RETRY_MS) {
    lastCloudAttempt = millis();
    Blynk.connect(1000);
  }
  if (Blynk.connected()) Blynk.run();

  const bool clockNeedsSync = !rtcReady || (millis() - lastNtpSync >= NTP_RESYNC_MS);
  if (clockNeedsSync && millis() - lastNtpAttempt >= NTP_RETRY_MS) {
    lastNtpAttempt = millis();
    if (!syncRtcFromNtp() && !rtcReady) Serial.println("NTP sync failed; local clock unavailable");
  }
}

void handleButton() {
  const bool reading = digitalRead(PIN_STOP);
  if (reading != lastButtonReading) {
    lastButtonReading = reading;
    lastButtonChange = millis();
  }
  if (millis() - lastButtonChange >= BUTTON_DEBOUNCE_MS && reading != stableButtonState) {
    stableButtonState = reading;
    if (stableButtonState == LOW && (alarmActive || snoozePending)) stopAlarm();
  }
}

void updateTimeInput(uint8_t index, BlynkParam param) {
  if (index >= 4) return;
  TimeInputParam t(param);
  if (!t.hasStartTime()) return;
  const int h = t.getStartHour();
  const int m = t.getStartMinute();
  const int s = t.getStartSecond();
  if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59) return;
  const uint32_t newTime = (uint32_t)h * 3600UL + (uint32_t)m * 60UL + (uint32_t)s;
  if (slots[index].secondsAfterMidnight != newTime) {
    slots[index].secondsAfterMidnight = newTime;
    slots[index].lastTriggeredDay = 0;
    saveSlot(index);
    drawDisplay();
    publishStatus();
  }
}

BLYNK_CONNECTED() {
  Blynk.syncVirtual(V0);
  Blynk.syncVirtual(V1);
  Blynk.syncVirtual(V2);
  Blynk.syncVirtual(V3);
  Blynk.syncVirtual(V4);
  Blynk.syncVirtual(V5);
  Blynk.syncVirtual(V6);
  Blynk.syncVirtual(V7);
  publishStatus();
}
BLYNK_WRITE(V0) { updateTimeInput(0, param); }
BLYNK_WRITE(V1) { updateTimeInput(1, param); }
BLYNK_WRITE(V2) { updateTimeInput(2, param); }
BLYNK_WRITE(V3) { updateTimeInput(3, param); }
BLYNK_WRITE(V4) { slots[0].enabled = param.asInt() == 1; saveSlot(0); drawDisplay(); publishStatus(); }
BLYNK_WRITE(V5) { slots[1].enabled = param.asInt() == 1; saveSlot(1); drawDisplay(); publishStatus(); }
BLYNK_WRITE(V6) { slots[2].enabled = param.asInt() == 1; saveSlot(2); drawDisplay(); publishStatus(); }
BLYNK_WRITE(V7) { slots[3].enabled = param.asInt() == 1; saveSlot(3); drawDisplay(); publishStatus(); }
BLYNK_WRITE(V8) { if (param.asInt() == 1) stopAlarm(); }
BLYNK_WRITE(V9) { if (param.asInt() == 1) snoozeAlarm(); }

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUZZER, OUTPUT);
  setBuzzer(false);
  pinMode(PIN_STOP, INPUT_PULLUP);

  Wire.begin(PIN_SDA, PIN_SCL);
  displayReady = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (!displayReady) Serial.println("OLED initialization failed; continuing without display");

  rtcReady = rtc.begin();
  if (!rtcReady) Serial.println("DS3231 not detected");
  prefsReady = prefs.begin("smartmed", false);
  if (!prefsReady) Serial.println("NVS unavailable; schedules will not survive power loss");
  loadPersistentState();

  if (rtcReady && rtc.lostPower()) rtcReady = false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Blynk.config(BLYNK_AUTH_TOKEN);

  if (rtcReady && alarmMask) {
    if (snoozePending && rtc.now().unixtime() >= snoozeDueEpoch) {
      snoozePending = false;
      snoozeDueEpoch = 0;
      alarmActive = true;
      saveAlarmState();
    }
    setBuzzer(alarmActive);
  } else if (!rtcReady) {
    alarmActive = false;
    snoozePending = false;
    setBuzzer(false);
  }
  drawDisplay();
}

void loop() {
  serviceNetwork();
  handleButton();
  checkScheduledReminders();
  checkSnooze();

  if (millis() - lastStatusUpdate >= STATUS_UPDATE_MS) {
    lastStatusUpdate = millis();
    drawDisplay();
    publishStatus();
  }
  delay(2);
}
