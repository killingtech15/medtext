# Pre-push audit — 20 issues corrected

1. **Wrong Blynk Time Input parsing:** original code treated Time Input as a scalar integer. Corrected to `TimeInputParam` and validated start time.
2. **Datastream documentation mismatch:** clarified String datastreams with Time Input helper parsing.
3. **Unsafe `syncAll()`:** reconnect previously synchronized momentary Stop/Snooze controls. Now only V0–V7 schedule/config pins sync.
4. **Stale action replay:** V8/V9 are excluded from reconnect sync so a stored `1` cannot replay a stale command.
5. **Blocking physical-button loop:** removed wait-until-release loop; main loop remains responsive.
6. **Weak button handling:** replaced edge handling with non-blocking debounce state machine.
7. **Alarm lost on reboot:** persist active alarm mask and restore active audible alarm when RTC is valid.
8. **Snooze lost on reboot:** persist snooze state and due epoch; restore remaining snooze after restart.
9. **Snooze expiry state:** clear and persist snooze fields consistently when it expires.
10. **Exact-minute scheduling fragility:** added a bounded 90-second due grace window to tolerate short loop/network stalls.
11. **Simultaneous reminders:** aggregate all slots due in the same scan into one alarm mask instead of silently losing all but the first.
12. **Edited schedule blocked for the day:** reset that slot's last-fired date when its time changes.
13. **Invalid Time Input payload:** ignore empty/invalid values and validate hour/minute/second ranges.
14. **Timezone ambiguity:** specify daily local-time configuration and disable timezone/day/start-stop options in the app instructions.
15. **RTC lost-power ordering:** check lost-power status before trusting the clock; only trust time after NTP adjustment.
16. **No recovery after booting offline:** add periodic NTP retries whenever the clock is invalid and Wi-Fi later returns; restore any persisted active/snoozed alarm after time becomes valid.
17. **RTC detection failure hidden:** report missing RTC and keep scheduling disabled until time is valid.
18. **OLED init result ignored:** check display initialization and continue safely without a display.
19. **Preferences init result ignored:** check NVS availability and explicitly warn that persistence is degraded if unavailable.
20. **Unused timer object:** remove unused BlynkTimer instance and associated unnecessary dependency/state.

Additional hardening: non-blocking Wi-Fi startup, bounded reconnect cadence, persisted slot validation, common-ground/driver safety instructions, explicit offline-notification limitation, status handling for invalid clock, and multi-slot notification reporting.

## Validation status
- Source-level audit and static consistency review performed.
- C++ syntax-only check passed against local Arduino/Blynk API stubs (this catches language syntax errors, not real-library compatibility).
- Static assertions passed for TimeInput parsing, safe sync pins, persisted alarm/snooze state, non-blocking button handling, OLED/RTC checks, and offline alarm path.
- No Arduino CLI / PlatformIO toolchain is available in this environment, so this source has **not** been compiled against installed ESP32/Blynk libraries or flashed to physical hardware.
- Hardware-specific buzzer polarity/current, OLED address, RTC module voltage tolerance, and actual Blynk account/widget setup still require bench verification.
