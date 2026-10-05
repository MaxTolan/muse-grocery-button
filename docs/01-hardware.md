# Hardware: Waveshare ESP32-S3-Touch-AMOLED-1.75C (with battery)

Ordered variant: **ESP32-S3-Touch-AMOLED-1.75C with the included 3.7 V lithium battery**
(SKU 33691) — not the battery-less `-EN` version. The battery ships installed inside the
aluminum case.

## What's on the board

- MCU: ESP32-S3
- Display: 1.75" round **capacitive AMOLED** touchscreen — used for status, not for the main
  interaction (this is a voice-first device; the screen shows e.g. "Listening…" / "Added ✓").
- Audio in: **dual microphones with echo cancellation** — good for kitchen noise.
- Audio out: audio codec + **built-in speaker** — required for spoken confirmations.
- Buttons: programmable PWR and BOOT buttons — PWR (or BOOT) is the push-to-talk button.
- Power: AXP2101 battery-management chip + 3.7 V LiPo in the case. Rechargeable over USB.
- Radio: Wi-Fi (2.4 GHz). No permanent power wire — it lives on the fridge on battery.
- Case: aluminum, closed. Zero-solder build — do not design anything that needs the case opened.

## Mounting

Magnetic dots (owner-purchased separately, already on hand). The case back just needs to sit
flat against them — no firmware involvement, but keep the back clear of anything that would
interfere with a magnet mount.

## Order info

- Waveshare order #261003-012213-E0, $53.19 ($41.99 board + $11.20 Registered Post Air Mail,
  12–28 days). Paid Oct 2, 2026; ships after the Oct 5 holiday — expect late October.
- Earlier unpaid order #261003-010338-E0 was canceled; no duplicate charge.

## SDK note

This board **is** in the muse-gadget-sdk's supported-board list (it was the reference for the
todo-display's 5" port), so no board-support work is needed if you take the Gadgets path.
Plain ESP-IDF is also acceptable — document your choice in the repo.
