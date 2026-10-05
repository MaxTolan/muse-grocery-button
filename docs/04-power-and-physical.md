# Power and physical design

## Power strategy (AXP2101)

- The device sleeps deeply between uses; the PWR/BOOT button is the wake source.
- Wake → connect Wi-Fi → record → transact → speak → back to sleep. Wi-Fi stays up only for
  the session.
- Show a low-battery spoken warning ("Battery's getting low — time for a charge") and a
  small battery icon on the display; never let it die mid-sentence if avoidable.
- Charge over USB-C with the case closed (port accessible per Waveshare's design).
- **Do not state battery-life figures anywhere** — not in docs, strings, or comments. The
  owner explicitly forbids it.

## Physical

- Lives on the refrigerator door via magnetic dots (owner-supplied). Keep the case back
  flat and clear.
- Kitchen environment: the dual mics have echo cancellation, but keep the record path
  robust to fridge hum and background noise — a light noise gate is fine, heavy DSP is not
  required for v1.
- The aluminum case is the enclosure. No modifications, no exposed contacts, nothing that
  prevents it sitting flat on the fridge.
