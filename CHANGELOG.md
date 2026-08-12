# Changelog

## 0.1.0 (unreleased)

Initial release.

- Dock with sign-in via browser pairing, destination management, live
  per-destination status, disconnect protection countdown, and diagnostics.
- One-click routing of the OBS stream output to the relay, with a snapshot
  of the previous service and a restore action.
- Clean session end on Stop Streaming and on OBS exit, so platforms end
  immediately instead of holding on the standby screen.
- End-stream hotkey.
- Vertical canvas: a 9:16 counterpart of every scene, sharing your sources
  with its own layout and visibility, published as a second SRT ingest for
  portrait destinations.
- Scene switches transition on the vertical canvas with a copy of the
  transition OBS ran, started at the same moment, including per-scene
  transition overrides.
