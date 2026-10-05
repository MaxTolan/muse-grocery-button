# Conversation design

The button talks like a person, briefly. Every prompt below is a starting script — keep the
wording short and natural; the exact phrasing can evolve.

## Happy path

- Owner (holding button): "Fairlife skim milk, 52 ounce."
- Button: "Added Fairlife skim milk to Kroger. Anything else?"
- Owner: "That's it." / releases without speaking / single-presses to end.
- Button: "Got it." (short idle chirp is fine; then sleep)

## Store not specified

- Owner: "Eggs."
- Button: "Costco or Kroger?"
- Owner: "Costco."
- Button: "Added eggs to Costco. Anything else?"

## Store not answered

- Ask once more: "Sorry — Costco or Kroger?"
- Still no answer: "I'll put eggs on the Kroger list." (Kroger is the default; the owner can
  correct later by voice or chat.)

## Didn't catch it

- Transcription empty or garbage: "I didn't catch that — try again?"
- Two failures in a row: "Let's try once more, nice and slow." Then sleep; don't nag.

## Session flow

- "Anything else?" keeps the session open for ~10 seconds of follow-up items without
  re-pressing. Each new hold-to-talk adds another item.
- Single short press (no hold) ends the session with "Got it."
- 30 seconds of silence ends the session silently (no goodbye speech; just sleep).

## Display (round AMOLED)

Voice-first; the screen is status only:
- "Listening…" while recording
- "Thinking…" during transcription
- "Added ✓" + store name on confirmation
- Never full grocery lists on the 1.75" screen — too small, not the point.

## What NOT to do

- No multi-level menus, no settings trees on the device.
- No order placement from the device — the $40 Kroger prompt and any ordering happen
  agent-side with the owner in chat.
- No battery-life promises in any prompt or string.
