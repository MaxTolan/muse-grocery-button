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
