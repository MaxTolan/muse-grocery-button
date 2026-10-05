# Voice pipeline

## Stages

```
[hold button] → mic capture → upload → cloud transcription → text
    → agent-side disambiguation (store choice) → TTS prompt → speaker
    → [owner answers] → confirmed item → voice inbox → Muse files it
```

### 1. Capture (device)

- Press-and-hold to record, release to stop. Show "Listening…" on the round display while held.
- Cap recordings at ~15 seconds; auto-stop with a gentle "too long, try again" prompt.
- Sample at 16 kHz mono — plenty for speech transcription, kind to the battery and Wi-Fi.

### 2. Transcription (cloud function, unbuilt)

- Device uploads the audio clip; a small cloud function returns plain text.
- Provider NOT chosen (Whisper API, Deepgram, etc. — see `05-open-questions.md`). Pick one
  with the owner; optimize for short grocery utterances, not meetings.
- Keep the round trip fast: the owner is standing at the fridge. Target < 3 s from release
  to spoken confirmation.

### 3. Store disambiguation (device + agent)

- If the owner names a store ("Costco milk"), use it.
- If not, the device asks: "Costco or Kroger?" (see `03-conversation-design.md`).
- Default rule if the owner doesn't answer: ask once more, then file to Kroger and say so.
  (Owner can correct by voice or chat; Muse handles corrections.)

### 4. TTS (device)

- Provider NOT chosen — pick with the owner. Needs a natural voice, short prompts.
- Cache nothing sensitive; prompts are generated per interaction.

### 5. Voice inbox → Muse (mechanism TBD — do not guess)

The confirmed item must land somewhere the owner's Muse agent reads on its morning check
(the same check that enforces the Kroger $40 rule). Options include an HTTPS endpoint the
agent polls, a message queue, or the Gadgets platform's agent channel if one exists.
**Do not invent this** — the transport is an owner decision (`05-open-questions.md`).
Build the device side against a stub `submit_item(text, store)` function with a clean seam.

## Latency budget (design target)

- Release → transcription text: < 2 s
- Text → spoken confirmation starts: < 1 s
- Total press-to-confirmation: < 5 s for the common case
