# Changelog

## 0.3.1

### Mobile audio

- You can now choose what is heard on mobile. The DualStream Mobile Sources
  panel ends with a list headed "Heard on mobile": every source in the scene
  that has audio and every audio device from Settings, Audio, each with a
  check box. Everything is ticked until you untick it, and your desktop
  stream is never affected. Twitch dual format is one broadcast, so its
  mobile picture keeps the desktop stream's audio; the list says so whenever
  a dual format destination is on.

### Changed

- One name for each side of your stream everywhere in the plugin: Desktop
  and Mobile. The panels formerly called DualStream Vertical and DualStream
  Vertical Sources are now DualStream Mobile and DualStream Mobile Sources,
  a destination sends to Desktop, Mobile or Desktop and mobile, and no note
  speaks of vertical, portrait or landscape any more.

### Fixed

- When Twitch declines dual format, the panel and OBS's error dialog now
  show Twitch's own reason instead of a bare code. A fractional frame rate
  such as 29.97, which Twitch refuses, is called out before you go live.
- Any source shown on both desktop and mobile (a media source, a browser
  source, a game or window capture with audio) was heard twice, about 6 dB
  louder, on every stream and recording while Mobile was on. Mobile no longer
  mixes its own audio into your streams; an existing setup is rebuilt once,
  the next time nothing is streaming or recording.

## 0.3.0

### Twitch dual format

- A Twitch destination can now be set to dual format. Your landscape and
  vertical scenes go out to Twitch as one Enhanced Broadcasting session, with
  every rendition Twitch asks for encoded here and carried to the relay on
  one connection. Turn on the vertical canvas, set the Twitch destination to
  "dual format", and press Start Streaming as always.
- The relay is now a streaming service of its own in OBS rather than a custom
  server. OBS applies the relay's keyframe interval and bitrate cap at every
  stream start the way it does for any listed platform, refuses an encoder
  the relay cannot take before the stream begins, and a connected platform
  account can no longer overwrite the relay stream key. Profiles routed by
  an earlier release move over on their own.

### Fixed

- A profile that once had Enhanced Broadcasting turned on for Twitch could
  not start a relay stream at all: OBS refused every Start Streaming press
  with a missing configuration dialog. The switch is now turned off for a
  relay profile, and the panel says when a restart is needed for it to take.
- The panel no longer says the relay tops out at 1080p and re-encodes what
  it receives. It carries what you send; the notes now give the relay's real
  figures, and a keyframe interval or bitrate the relay would refuse is
  called out before the stream starts rather than discovered as a quality
  drop mid-stream.
- The relay panel now says when the mobile side of your stream is off, or on
  with nothing taking it, and can turn the vertical canvas on from there.
- Vertical layouts remember the canvas size they were made for and are
  scaled to a new one, when the relay's portrait size changes, the next
  time no output is running.

## 0.2.1

### Fixed

- Streams no longer fail with an endless reconnect loop when OBS has a
  connected platform account. OBS quietly swaps in that account's own stream
  key at every stream start; the relay key is now put back before the
  connection opens, every time. The panel still recommends disconnecting the
  account, but it no longer breaks streaming.
- Your relay ingest details are re-checked at every stream start and
  refreshed when they have been sitting unused, so a key rotated from the
  website no longer strands OBS on the old one.
- Going live over SRT starts a few seconds sooner. The plugin was asking for
  a larger network recovery window than the relay uses, which delayed every
  stream start by that difference; it now follows the relay's window.
- While you are live, a brief hiccup reaching DualStream no longer flips the
  whole panel to Offline. Your stream was never affected; now the panel says
  so too, in a note, while staying on Live.
- If a stream ends on an error, the panel now says what happened, in plain
  words, with the exact error underneath, instead of returning to Ready as
  if nothing happened. The same detail lands in Copy diagnostics.
- The panel now warns before you go live when your encoder is set to a video
  or audio format the relay cannot take, in simple output mode as well as
  advanced. Previously the stream connected fine and reached no one.
- If your sign-in expires, the panel asks you to sign in again instead of
  silently signing you out and forgetting the stream keys you had typed in.
  Those now survive until you sign out yourself.
- The settings saved by "Restore previous settings" are now encrypted on
  disk, the same way as everything else the plugin stores, and are no longer
  overwritten once taken.

## 0.2.0

### Try it without a subscription

- You can now set up one streaming destination without an active
  subscription. Enter a server address and a stream key from any platform,
  and choose whether it carries your normal scenes or your vertical ones.
- Vertical streams start from a **Go live** button in the DualStream Vertical
  panel. OBS's own Start Streaming button always sends your normal scenes, so
  it cannot carry a vertical one. The panel tells you which button to press.
- That button now says what is happening while it works: starting, live, and
  ending each look different, and it cannot be pressed twice by mistake.
- Once saved, the destination tidies itself into a single line showing where
  it sends and which scenes it carries, with a pencil to change it. Your
  stream key is never shown back to you.
- Everything about that destination stays on your computer, encrypted.

### Fixed

- A vertical stream could be running with the button still offering to start
  it, leaving no obvious way to stop.
- Stopping a vertical stream while it was still connecting could leave it
  stuck.
- Adding a destination without an active subscription said "Something went
  wrong" instead of explaining that the subscription was inactive.

## 0.1.0

First release. Windows only.

### Streaming

- A panel in OBS that signs you in through your browser, manages where your
  stream goes, and shows each platform's status while you are live.
- One upload reaches every platform you turn on, so adding platforms does not
  cost you extra upload speed.
- If your connection drops, your platforms stay live on your standby screen
  and the panel counts down how long you have to reconnect.
- Pressing Stop Streaming ends every platform straight away rather than
  leaving them on the standby screen, and the same happens if you quit OBS.
- Turning a platform on or off mid-stream asks first, then shows the row as
  applying until it takes effect. You do not have to stop streaming.
- A keyboard shortcut to end the stream everywhere.

### Vertical scenes

- Every scene gets a matching 9:16 version for TikTok, Reels and Shorts. It
  shares the same cameras and sources as your normal scenes, but you arrange
  them separately and choose which ones appear.
- Switching scenes uses the same transition on both, at the same moment,
  including any per-scene transition you have set.
- Arranging sources works the way it does in OBS: the same drag handles,
  alt-drag to crop, spacing guides, out-of-bounds shading, and a right-click
  menu for adding sources, transforms and ordering.
- The vertical sources list is its own panel, so it can sit beside the OBS
  one instead of being squeezed underneath.

### Destinations

- Accounts you have already added are shown rather than hidden, so the list
  no longer looks empty once you have added everything.
- A button to manage your connected accounts, which has to be done on the
  web.
- TikTok, Facebook Live and Facebook Reels fill in their server address and
  orientation for you, leaving only the stream key to paste.
- Your own server addresses and stream keys are remembered so the edit screen
  can show what a destination is set to, with a button to reveal the key.

### Privacy and security

- Your sign-in is encrypted on your computer using Windows' own protection,
  tied to your Windows account. Anything saved by an older version is
  upgraded the first time it is read.
- Sign-in details are sent only to DualStream, over a verified connection
  that refuses redirects, so they cannot be forwarded anywhere else.
