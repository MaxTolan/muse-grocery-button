# Muse Grocery Button

A battery-powered **push-to-talk button for the refrigerator**. Hold it, say a grocery item,
and it lands on the owner's Costco or Kroger grocery list. It speaks back through its
built-in speaker — confirming what it heard and asking follow-ups like "Costco or Kroger?"
Think an old Amazon Dash button, but smarter and voice-driven. Rechargeable, no power wire.

## Hardware

**Waveshare ESP32-S3-Touch-AMOLED-1.75C with battery** (SKU 33691): 1.75" round capacitive
AMOLED touchscreen, dual mics with echo cancellation, audio codec + built-in speaker,
programmable PWR/BOOT buttons, Wi-Fi, AXP2101 battery management, 3.7 V LiPo installed in
the aluminum case. Zero-solder build. Mounts magnetically on the fridge.

## Documentation (start here if you're a coding agent)

| File | What it covers |
| --- | --- |
| `docs/00-start-here.md` | Intent, end-to-end flow, priorities, hard constraints, status |
| `docs/01-hardware.md` | Board spec, order info, SDK note |
| `docs/02-voice-pipeline.md` | Capture → transcription → TTS → voice-inbox handoff, latency budget |
| `docs/03-conversation-design.md` | Spoken scripts: confirmations, follow-ups, failure recovery |
| `docs/04-power-and-physical.md` | AXP2101 sleep/wake, fridge mounting, kitchen environment |
| `docs/05-open-questions.md` | The owner's decisions (answered Oct 21) |
| `docs/06-firmware.md` | The firmware: decisions, how it works, setup, build, tests |

## Status

Board ordered and paid Oct 2, 2026 — arrives late October. The firmware is written (Oct 21) and
builds for the 1.75C: hold-to-talk with Google Speech-to-Text, the spoken script with Google
Text-to-Speech, a voice inbox that sends confirmed items to Muse, and deep sleep between uses.
It hasn't run on the board yet. Start at `docs/06-firmware.md`; build with `tools/fw.sh build`,
test with `tools/test.sh`.

## How it fits the owner's system

Confirmed items go into a voice inbox that the owner's Muse agent reads each morning and
files into his Costco/Kroger grocery lists. Kroger carries an estimated running total —
when it tops $40, the agent asks him whether he's ready to place the order (estimates only).
List management, pricing, and ordering are agent-side, never firmware.
