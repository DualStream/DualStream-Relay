# Changelog

## 0.1.0 (unreleased)

Initial release. Windows only: the macOS and Linux build targets are
configured but are not yet producing working builds.

### Streaming

- Dock with sign-in via browser pairing, destination management, live
  per-destination status, disconnect protection countdown, and diagnostics.
- One-click routing of the OBS stream output to the relay, with a snapshot
  of the previous service and a restore action.
- Clean session end on Stop Streaming and on OBS exit, so platforms end
  immediately instead of holding on the standby screen.
- End-stream hotkey.
- Turning a destination on or off mid-stream asks first, then shows the row
  as applying until the relay confirms it. Off-air the switch stays
  immediate.

### Vertical canvas

- A 9:16 counterpart of every scene, sharing your sources with its own
  layout and visibility, published as a second SRT ingest for portrait
  destinations.
- Scene switches transition on the vertical canvas with a copy of the
  transition OBS ran, started at the same moment, including per-scene
  transition overrides.
- Preview editing matches OBS: the same handles, crop on alt-drag, spacing
  helpers, out-of-bounds shading, and a right-click menu carrying add
  source, transform, order, filters and properties.
- Sources list in its own dock, so it can sit beside the OBS sources panel.
- Turned on and off from the relay dock's settings, not from closing the
  dock, since turning it off discards the vertical layouts.

### Destinations

- Connected accounts already in use are listed with an Added badge rather
  than hidden, so the section no longer looks empty once every account has
  been added.
- A link to manage connected accounts, which cannot be done from OBS.
- TikTok, Facebook Live and Facebook Reels prefill their ingest server and
  orientation, leaving only the stream key to enter.
- Custom RTMP server and stream key are cached locally so the edit dialog
  can show what a destination is set to, with a reveal toggle on the key.

### Security

- Sign-in tokens are encrypted at rest with the operating system's own
  facility, tied to the signed-in user account. Existing plain-text token
  files are migrated on first read. No plain-text fallback is written on
  platforms without a secret store.
- HTTP redirects are refused rather than followed. The bearer token travels
  as a custom header, which curl repeats to a redirect target whatever host
  or scheme it names, so the token now only ever reaches the configured
  host.
- The HTTP client is restricted to https and http, out of curl's much wider
  default protocol set.
- Certificate and hostname verification are set explicitly rather than left
  to library defaults.
- Destination identifiers are percent-encoded before going into URL paths.
