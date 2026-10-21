# Open questions (ask the owner — do not guess)

1. **Transcription provider**: Whisper API, Deepgram, or something else? (Optimized for short
   grocery utterances; cost per clip matters more than meeting features.)
2. **TTS provider**: natural voice for short prompts; same cost sensitivity.
3. **Voice-inbox transport**: how do confirmed items reach the Muse agent — HTTPS endpoint the
   agent polls, a queue, or the Gadgets platform channel? This is the biggest architectural
   decision; build against the `submit_item(text, store)` stub until it's decided.
4. **Gadgets SDK vs plain ESP-IDF**: the board is in the SDK's board list, so the Gadgets
   path is easy — but plain ESP-IDF is fine too. State your choice and why.
5. **Wi-Fi provisioning**: how do network credentials get onto the device? (Simplest v1:
   USB serial config at the bench; the owner can do this once.)
6. **Cloud function hosting**: where does the transcription relay live? (Owner's call —
   keep it cheap and simple.)

## Answers (owner, Oct 21, 2026)

1. Transcription: **Google Cloud Speech-to-Text**, called from the device.
2. TTS: **Google Cloud Text-to-Speech**, same key.
3. Voice-inbox transport: **a chat message to the owner's Muse** through the Gadgets SDK, one per
   batch of confirmed items, kept in flash until Muse acknowledges it.
4. **Gadgets SDK**: the 1.75C is its reference board, and it brings Wi-Fi setup and the Muse link.
5. Wi-Fi provisioning: **the Muse app's pairing** (it sends Wi-Fi over BLE).
6. Hosting: **none needed**; the device calls Google directly.

Details and reasoning: `06-firmware.md`.

