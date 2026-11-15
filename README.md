# medtext — SmartMed ESP32 Medicine Reminder

A student prototype with four daily reminder slots, local alarm scheduling, remote Blynk controls, and phone push notifications.

## Features
- Four daily medicine times configured from the Blynk Android app.
- DS3231 RTC keeps time when internet is unavailable.
- ESP32 Preferences (NVS) stores schedules and unfinished alarm/snooze state.
- Active buzzer rings locally at scheduled times.
- Physical push button dismisses an active or snoozed alarm.
- Remote Stop and 5-minute Snooze controls through Blynk Cloud.
- Blynk event notification when a reminder becomes due (requires internet).
- OLED displays current time, next reminder, alarm or snooze state.
- Simultaneous reminders are combined into one audible alarm, with an event for each due slot.

**Offline behavior:** Scheduled alarms continue locally without Wi-Fi/cloud. Phone notifications and remote commands cannot work while the device is offline. The RTC must have valid time and its CR2032 backup cell must be installed.

## Hardware used
- ESP32 DevKit (30- or 38-pin)
- DS3231 RTC module with CR2032
- 0.96-inch SSD1306 I2C OLED
- Active buzzer
- One tactile push button
- Breadboard, jumper wires, 5V USB supply

The four reed switches, three spare buttons and LEDs in the original shopping list are not needed for this scope.

## Buzzer driver safety
Check the actual buzzer module. A bare 5V buzzer or any buzzer whose current exceeds safe GPIO capability must use a suitable transistor/MOSFET driver. For a simple NPN driver: 2N2222/BC547, 1kΩ base resistor, buzzer positive to +5V, buzzer negative to collector, emitter to GND, GPIO25 through 1kΩ to base. Join ESP32 and 5V supply grounds. Verify transistor pinout and buzzer datasheet. Never connect 5V to an ESP32 GPIO.

## Wiring
| ESP32 | Connects to |
|---|---|
| 3V3 | OLED VCC and DS3231 VCC (assuming breakout supports 3.3V) |
| GND | OLED GND, DS3231 GND, button ground, driver ground |
| GPIO 21 | OLED SDA and DS3231 SDA |
| GPIO 22 | OLED SCL and DS3231 SCL |
| GPIO 25 | Buzzer driver input (not the 5V buzzer supply) |
| GPIO 27 | One side of normally-open stop button; other side to GND |

The OLED and RTC share the I2C bus. Check the OLED address; firmware defaults to 0x3C (some boards use 0x3D). Power ESP32 through its USB connector. Do not feed 5V into 3V3.

## Firmware setup
1. Install Arduino IDE and Espressif ESP32 board support.
2. Install libraries: Blynk, RTClib by Adafruit, Adafruit GFX, Adafruit SSD1306.
3. Open `firmware/SmartMed.ino`.
4. Replace `BLYNK_TEMPLATE_ID`, `BLYNK_TEMPLATE_NAME`, `BLYNK_AUTH_TOKEN`, `WIFI_SSID`, and `WIFI_PASS`.
5. Select the correct ESP32 board and port; compile and upload.
6. Open Serial Monitor at 115200 baud.

Do not commit real Wi-Fi credentials or Blynk tokens. Use placeholders in the public repository and enter secrets locally before upload.

## Blynk setup
Create a Blynk IoT template (ESP32 / Wi-Fi), create a device, and copy its template ID/name and device auth token into firmware. Install the official Blynk IoT Android app and log in to the same account.

### Datastreams
| Pin | Type | Purpose |
|---|---|---|
| V0–V3 | String | Time Input for medicine slots 1–4 |
| V4–V7 | Integer, 0–1 | Enable/disable slots 1–4 |
| V8 | Integer, 0–1 | Remote Stop command |
| V9 | Integer, 0–1 | Remote Snooze command |
| V10 | String | Alarm status (device -> app) |
| V11 | String | Next reminder (device -> app) |
| V12 | String | Current time (device -> app) |

### App widgets
- Four Time Input widgets on V0–V3. Set format HH:mm or HH:mm:ss. Disable start/stop, day-of-week, sunrise/sunset, and timezone selection. These are daily local-time reminders.
- Four Switch widgets on V4–V7.
- Two Button widgets on V8 and V9, both in **PUSH** mode. Label them `Stop alarm` and `Snooze 5 min`.
- Three Label/Value Display widgets on V10, V11 and V12.

Blynk Time Input datastreams are String, but the firmware parses them with Blynk's `TimeInputParam` helper. Empty time input payloads are ignored. The firmware syncs only V0–V7 on reconnect; action pins V8/V9 are intentionally excluded to avoid replaying stale button commands.

### Push notification event
Create a Blynk event with event code exactly `medicine_due` and enable push notifications for the event. Firmware calls `Blynk.logEvent()` when the local reminder starts. Event notifications require Wi-Fi and Blynk Cloud connectivity; a reminder that occurs while offline still rings locally but cannot send a push notification retroactively.

## Clock behavior
At boot, the device tries to connect to Wi-Fi. When online, it obtains time via NTP using India Standard Time (UTC+05:30) and sets the DS3231. The RTC then continues timekeeping offline using its coin cell. If the RTC reports lost power and NTP is unavailable, the firmware refuses to invent a time and will not trigger schedules until it can synchronize successfully.

The device retries NTP while the clock is invalid and resynchronizes approximately every 24 hours. Keep the CR2032 installed.

## Alarm behavior
- The schedule is checked locally against the RTC every loop.
- A 90-second grace window catches brief loop stalls. It intentionally does not sound old reminders missed by hours.
- Each slot fires once per calendar day. Editing a time resets that slot's fired marker so a changed time can still fire later that day.
- If multiple slots share a due time, they are combined into one audible alarm and each gets its own Blynk event.
- Stop dismisses the current alarm; it does not delete the daily schedule.
- Snooze silences the active alarm for five minutes. The pending alarm and snooze deadline are persisted and restored after reboot if RTC time is valid.
- If a new scheduled dose becomes due during a snooze, the device rings for the new due reminder.

## Test checklist
1. Confirm OLED time is correct and RTC has a coin cell.
2. Configure one reminder 2–3 minutes ahead and enable it.
3. Verify buzzer and Blynk push notification.
4. Test remote Stop and physical Stop.
5. Test Snooze and verify it rings again after five minutes.
6. Test two slots at the same time; confirm one combined alarm.
7. Disconnect Wi-Fi and verify a previously saved schedule still rings locally.
8. Reconnect Wi-Fi and verify app schedule changes sync.
9. Power-cycle during an active alarm and during snooze; verify state recovery.
10. Test phone notification permissions and battery optimization on the actual Android phone.

## Known limitations and safety
- This is a reminder prototype, not a medication dispenser or adherence verification device. A button press or compartment opening does not prove medicine was swallowed.
- No push notification or remote action is possible while home Wi-Fi/internet/cloud connectivity is unavailable.
- If power is lost and the RTC backup battery is dead, time must be synchronized again before reminders run.
- The 90-second grace period is not a missed-dose escalation system.
- Test thoroughly before relying on it for real medication. Keep electronics insulated and away from liquids and loose medication.
