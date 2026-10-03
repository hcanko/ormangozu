# v0.6.1 — Targeted Nest control pilot

Based on v0.6.0 merged source; old dashboard, CSV import, mesh alerts, LittleFS and OTA paths retained.

- Added authenticated, addressed LoRa **OGC1/CMD** and **OGC1/RESULT** frames separate from OG4 autonomous Whisper traffic; shared pilot HMAC key, monotonic per-sender boot/command sequence persisted in NVS for replay rejection.
- Added controlled allowlist: `STATUS`, `SAMPLE`, `FAST`, `AUTO`, `MANUAL`, `PAN`, `TILT`, `HOME` (only absolute bounded angles). No remote arbitrary code, firmware over LoRa, log erase, or alarm disabling.
- Added Core-0 SensorTask control handler. Local/peer WATCH forces manual mode off. Time-limited manual mode defaults to 120 seconds; physical actuator PWM **disabled by default** until wiring/servo/power/limits are verified.
- Added `WHOAMI` / `CTRL id target op arg` USB syntax on the bridge Nest and a single outstanding job relay. USB `OGCTRL` JSON includes status and readings.
- Added local FastAPI `/api/control/*`: authenticated enqueue, atomic short lease, strict target/argument validation, result pairing, 45s queued TTL and 50s claimed TTL. Expired offline commands never run on late reconnection.
- USB collector now identifies its connected Nest, polls jobs, writes short serial commands, persists `OGCTRL` JSONL and forwards/replays results when API becomes available.
- Deprecated insecure legacy arbitrary IP command forwarding (HTTP 410). Old map/admin shortcuts now direct operators to the addressed Nest Kontrol panel.
- React `Nest Kontrol` screen added alongside existing Whisper Pilot. It requests actions; no fake local camera preview or claims of live thermal video.
- Isolated backend/collector integration test added.

**Known boundaries:** No independent gateway, remote internet OTA, validated servo movement or physical radio test. Complete two-board PlatformIO build/flash, timeout/loss/power and mechanical acceptance matrix before deployment.
