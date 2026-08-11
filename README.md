# DualStream Relay for OBS

Stream to multiple platforms from OBS Studio through the DualStream relay.
OBS sends one stream; the relay delivers it to every destination you enable
(Twitch, YouTube, Kick, or any custom RTMP target). If your connection drops
mid-stream, the relay keeps your channels live on your standby screen and
picks the stream back up when you reconnect.

The plugin adds a dock to OBS that manages destinations, shows per-platform
status while you are live, and ends the session cleanly when you press Stop
Streaming. It adds no second start button and never encodes or uploads video
itself: your existing Start Streaming button does what it always did, with
the stream routed through the relay.

Using the relay requires a DualStream account. Learn more at
[dualstream.gg/relay](https://dualstream.gg/relay).

## Features

- One upload, every destination. Destination count does not change your
  upload bandwidth.
- Disconnect protection: platforms stay live on your standby screen through
  connection drops, with an on-screen countdown in the dock.
- Destination management in OBS: connected accounts are one click, custom
  RTMP targets take a URL and a key, and enable/disable applies within a few
  seconds even mid-stream.
- Live status per destination, including the relay's own error message when
  a platform rejects the stream.
- Sign-in happens in your browser. The plugin never sees your password.

## Requirements

- OBS Studio 31.1 or newer (Windows, macOS, or Linux)
- A DualStream account

## Installation

Download the installer or archive for your platform from the releases page
and follow the standard OBS plugin installation steps. After restarting OBS,
the dock opens once automatically; it is also available under Docks and
under Tools in the menu.

## Building

The project uses the OBS plugin template build system. With CMake 3.28 or
newer installed:

```
cmake --preset windows-x64
cmake --build --preset windows-x64
```

The `macos` and `ubuntu-x86_64` presets work the same way on those
platforms. Dependencies (OBS sources, prebuilt libraries, and Qt) are
downloaded automatically as pinned in `buildspec.json`.

## Contributing

Contributions are welcome. Before your first commit, install the pre-commit
hook so the character checks run locally:

```
cp tools/hooks/pre-commit .git/hooks/pre-commit
```

The same checks run in CI on every pull request.

## Credits

Built on [OBS Studio](https://obsproject.com) and started from the
[OBS plugin template](https://github.com/obsproject/obs-plugintemplate).
Thanks to the OBS Project and its contributors for the platform and the
tooling that make plugins like this one possible.

## License

GPL-2.0-or-later. See [LICENSE](LICENSE).
