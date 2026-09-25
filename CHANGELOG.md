# Changelog

## 0.3.3

### Mobile

- Flipping a source now mirrors it where it stands. It used to mirror
  around the item's position, which pushed the picture off the frame.
- Fit to screen and Stretch to screen now set a plain scale on the item,
  so its outline is its picture: it selects, drags and resizes as itself
  rather than as a box the size of the whole frame. Layouts saved by the
  first release are converted the same way, once per scene, and a
  transform you set afterwards in OBS's Edit Transform dialog is kept.
- A source added before it has a picture, such as a capture that has not
  started or a page still loading, is placed in the frame the moment it
  reports a size, and once more before its scene goes on air.
- A mobile scene made from a desktop scene that was still empty now lays
  out the sources added to it later the way a new scene is laid out: the
  largest visible source fills the frame and is shown, the rest arrive
  hidden. Before, everything added to such a scene arrived hidden and the
  mobile frame stayed black.
- The Mobile Sources panel says when a scene has sources but none of them
  is shown on mobile, and which control shows one.
- In studio mode the mobile panels show and edit the preview scene, the
  one you are preparing, and the mobile preview draws it. The mobile
  stream keeps the program scene until you transition, as your desktop
  stream does.
- Selecting an item in the mobile preview selects it in the scene itself,
  so OBS's Edit Transform dialog follows your selection there too.

### Destinations

- Changing which picture a live destination sends now asks first and says
  what happens. On Twitch the change waits for the next go-live. Elsewhere
  the picture taken away ends its broadcast for this stream, and changing
  it back starts that picture again as a new broadcast. Adding a picture
  asks nothing.
- Saving a YouTube destination sends only the fields you changed, and
  clears a field you emptied. Everything the desktop app stored on the
  destination stays as it was.
- The YouTube dialog now explains how a stream scheduled in YouTube Studio
  is used: desktop takes it as is when it starts within 12 hours of going
  live, and mobile is its own broadcast that takes the dialog's fields
  first and copies the Studio stream for anything left empty.

## 0.3.2

### Dual format

- Twitch can ask for the mobile picture in HEVC, and for some channels it
  does so at 60 fps whatever it is told. The plugin now offers HEVC when
  this computer has a hardware HEVC encoder, encodes every rung in the codec
  Twitch dictated, and carries HEVC in the contribution. A computer without
  an HEVC encoder is asked for the desktop ladder alone instead.
- Start Streaming no longer fails over the ladder. Whatever goes wrong in
  preparing dual format, the stream starts: with the desktop ladder only, or
  with OBS's own encoder alone, and the panel says what was left out and why
  while you are live. Twitch's own reason is shown when it declined.

### Mobile

- The mobile preview showed a black canvas, and the mobile stream never
  started, when OBS already had an output running as it loaded the scene
  collection: a virtual camera or NDI feed started at launch, a recording,
  anything. OBS will not give a loaded canvas its video mix while any output
  runs, so the mobile canvas is now rebuilt with its mix in place instead.
- The mobile preview went white after its dock was floated, docked or moved
  to another screen. The preview now follows its window through those moves.
- OBS could crash a moment after Start Streaming with a mobile destination
  on: a second start request arriving while the mobile output was still
  connecting tore the first one down mid-connect.
- The mobile stream now goes out at the bitrate your desktop stream is
  tuned to, capped by the relay's figure for it, instead of a fixed
  6000 kbps. With both canvases on, your upload carries twice the desktop
  rate; before, it carried the desktop rate plus 6000.
- The panel now states the total upload both canvases take while a mobile
  destination is on, and the standby banner says when the cause is an
  upload that cannot keep up rather than a dropped connection.

### YouTube

- A stream you scheduled in YouTube Studio is now the one that goes live,
  with its own title, description, privacy and ad settings, instead of a
  new broadcast called "DualStream Live" with ads off beside it. The mobile
  broadcast takes the same title and settings. The destination dialog says
  so, and its own fields are overrides.
- Saving a YouTube destination no longer drops the settings the desktop app
  stored on it, and privacy can be left as set on YouTube instead of being
  forced to Public on every save.

### Relay

- The bitrate and keyframe figures in the panel's notes and the tune dialog
  now come from the relay, resolved for your account: a Twitch partner's
  line sits higher than everyone else's and is no longer flagged.
- A destination that is live but not getting what you sent, because the
  relay had to re-encode it, dual format was dropped, or a YouTube latency
  setting caps it, now says so under its row in plain words.
- The panel showed a destination as rejected while it was live, for the
  whole stream. The relay keeps a status row per attempt to carry a
  destination and the panel took the first one; it now shows the attempt
  that is running. The live and issue counts in the summary follow.

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
