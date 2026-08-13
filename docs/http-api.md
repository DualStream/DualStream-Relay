# HTTP surface the plugin speaks

The plugin is a thin client of the DualStream web API. It ships no secrets:
sign-in uses a browser pairing flow designed for public clients, and nothing
is embedded in the binary.

Two things are kept under the OBS module config directory: the bearer and
refresh tokens from sign-in, and the server and stream key of any custom
RTMP destination, the latter only so the edit dialog can show what a
destination is set to. Both are encrypted with the operating system's own
facility where one is wired up. See the README for what that means per
platform.

Base URL: `https://www.dualstream.gg`. The `DSRELAY_API_BASE` environment
variable overrides it for development.

Every request that carries a body sends `Content-Type: application/json`.
Authenticated requests send the token as `Authorization: Bearer <token>`
and duplicate it in `X-Auth-Token` for proxies that strip the standard
header. On a 401 the plugin refreshes the token once via
`POST /api/auth/refresh` and retries the request one time.

## Sign-in (browser pairing)

| Method | Path | Purpose |
|---|---|---|
| POST | `/api/auth/device/start` | Begin pairing. Returns `device_code`, `user_code`, `verification_url`, optional `interval` and `expires_in` |
| POST | `/api/auth/device/poll` | Poll with `device_code`. Pending until the user approves in the browser, then returns the token |
| POST | `/api/auth/refresh` | Exchange `refresh_token` for a fresh token pair |

The plugin shows `user_code` in the dock and opens `verification_url` in
the system browser. A pending poll is signaled by HTTP 428 or 202, or by a
body `code` of `authorization_pending`; `slow_down` increases the poll
interval. HTTP 410 means the code expired.

## Relay

| Method | Path | Used for |
|---|---|---|
| GET | `/api/relay/limits` | Destination limit, grace window |
| GET | `/api/relay/ingest-target` | Ingest server URL and key used to route the OBS stream output |
| GET | `/api/relay/destinations` | Destination list |
| POST | `/api/relay/destinations` | Create a destination |
| PATCH | `/api/relay/destinations/:id` | Edit, enable or disable |
| DELETE | `/api/relay/destinations/:id` | Remove |
| POST | `/api/relay/destinations/:id/test` | Reachability probe for custom RTMP destinations |
| GET | `/api/relay/discover` | One-click suggestions from connected accounts |
| GET | `/api/relay/sessions/current` | Live session status frame; polled every 3 seconds while streaming |
| POST | `/api/relay/sessions/end` | Signal an intentional stream end |
| GET | `/api/relay/settings` | Read the disconnect protection preference |
| PATCH | `/api/relay/settings` | Write the disconnect protection preference |

`POST /api/relay/sessions/end` is fired when OBS raises its streaming
stopping event and again as a bounded-wait fallback on OBS exit. It tells
the relay the stop was intentional, so the session ends immediately instead
of engaging disconnect protection for the full grace window.

A 404 from `/api/relay/sessions/current` is treated as the endpoint being
unavailable, not as an error; the dock then reports local information only.
