# Orman Gözü v0.6.1 — test report (source candidate)

## Checks actually run in this environment

- `python -m compileall -q backend tools tests`: PASS.
- `python -m unittest discover -s tests -v`: **13/13 PASS**, including v0.6 legacy integration, OG4 protocol reference and USB logging, OGC1 HMAC/replay test vectors, control API auth, per-device target, result correlation, TTL/expiry, disabled legacy HTTP command, and pilot data export.
- `cd backend && python -m pytest -q tests/test_smoke.py`: **2/2 PASS**.
- TypeScript global compiler TSX *syntax parser* (App, Layout, ControlCenter, DashboardMap, AdminPanel): **5/5 PASS**. This checks parse syntax only, NOT frontend build/typecheck.

## Cannot claim yet

- ESP/Heltec `.bin` **not built** here: PlatformIO/toolchain/dependencies not installed, previous environment failed network package retrieval. Actual target V3/V4, flash size and SX1262 GPIO still require checking.
- Frontend `npm ci && npm run build` **not completed** here: dependencies not installed/offline npm cache insufficient. GitHub CI checks were updated for v0.6.1.
- No live two-Nest LoRa, power, servo motion, outdoor sensor, or OTA hardware test. All controller functionality is source-code/test-candidate level.

## Safety defaults

- `OG_ENABLE_PAN_TILT=0`: remote servo movement returns `UNSUPPORTED` until mechanics, separate servo power and valid pins verified.
- Existing close-range maintenance AP/ArduinoOTA only; **no remote OTA via LoRa**.
- Local API must bind `127.0.0.1` and requires long `OG_CLIENT_TOKEN` for control; legacy unauthenticated IP forwarding endpoint returns 410.
- Pilot shared mesh key and HMAC packet authentication are **not** fleet-grade identity/access control or encryption.
- `AUTO` restores servo home (when installed); local or peer fire suspicion pre-empts manual mode. Camera moving still temporarily changes observed viewing sector.

See `REMOTE_CONTROL_v0.6.1.md` and `KALAN_ISLER_v0.6.1.md` for wiring/field acceptance.
