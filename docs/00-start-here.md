# START HERE (for the coding agent)

You are picking up firmware for **Maxwell Tolan's fridge push-to-talk grocery button**.
Read this file first, then `01-hardware.md` through `05-open-questions.md` in order. The
owner's intent and the hard constraints are all in these docs — do not invent features
beyond them.

## What the owner wants

A small battery-powered button that lives **magnetically on the refrigerator**. He holds the
button, says a grocery item out loud, and the item lands on his Costco or Kroger grocery
list. The button **speaks back**: it confirms what it heard and asks follow-up questions
(e.g. "Which store — Costco or Kroger?"). Think an old Amazon Dash button, but smarter and
voice-driven. Rechargeable, no permanent power wire.

## The end-to-end flow (device's part vs everyone else's)

1. **Hold to record.** Owner presses and holds the button, speaks, releases.
2. **Device** captures audio and sends it to a small cloud transcription function.
3. **Transcription** returns text (e.g. "fairlife skim milk").
4. **Device** speaks a confirmation + any follow-up question via TTS through its speaker
   (e.g. "Added fairlife skim milk to Kroger. Anything else?" / "Costco or Kroger?").
5. The confirmed item goes into a **voice inbox** that the owner's Muse agent reads; **Muse
   files it into the Costco/Kroger lists** — list management is agent-side, not firmware.
6. **Kroger rule (agent-side, for context):** Muse tracks an estimated running total for the
   Kroger list; when it tops $40, it asks the owner whether he's ready to place the grocery
   order. Estimates only; checkout may differ. Free delivery over $40.

The device handles: audio capture, transcription upload, TTS playback, button UX, Wi-Fi,
power management. It does NOT manage grocery lists, prices, or orders.

## What you are building (in priority order)

1. **Audio path**: mic capture → upload → transcription → TTS playback through the speaker.
   This is the core deliverable; get "press, speak, hear it back" working first.
2. **Conversation loop**: confirmation prompts and follow-up questions (script in
   `03-conversation-design.md`).
3. **Voice inbox handoff**: deliver confirmed items where the Muse agent can pick them up
   (mechanism TBD — see `02-voice-pipeline.md`; do not guess, ask the owner).
4. **Power management**: AXP2101-based sleep/wake so a single charge lasts as long as
   reasonably possible. Do NOT promise battery-life figures anywhere.

## Hard constraints (do not violate)

- Zero-solder build: everything must work with the board as shipped, in its aluminum case.
- The button mounts with magnetic dots the owner bought himself — firmware doesn't care,
  but the enclosure must stay closed and fridge-safe (no exposed contacts).
- Never promise battery life, in code comments, docs, or UI strings.
- Keep spoken prompts short and natural. No beeps-and-boops UX; it should feel like
  talking to a person.
- The same muse-gadget-sdk from the todo-display project applies if you use the Gadgets
  path; otherwise plain ESP-IDF is fine — say which you chose and why.

## Current status (as of Oct 5, 2026)

- Board ordered and paid Oct 2 (Waveshare order #261003-012213-E0, $53.19). Ships after the
  Oct 5 holiday; 12–28 days Registered Post Air Mail — expect it late Oct.
- Firmware, audio pipeline, transcription, TTS, and the voice-inbox handoff are all unbuilt.
- Transcription and TTS providers are NOT chosen yet — see `05-open-questions.md`.

## How to work

- Ask the owner about anything in `05-open-questions.md` before guessing.
- Get the audio loop working on the bench before worrying about the fridge, the case, or power tuning.
- Commit small, working increments. The `main` branch should always build.
