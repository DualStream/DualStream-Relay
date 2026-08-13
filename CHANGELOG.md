# Changelog

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
