---
status: pitched
pitched: 2026-11-19
started: ~
completed: ~
audited: ~
slices: ~
---

# Phone link: reach desktop apps from your phone without Tailscale

## Problem

Needful's phone page only reaches the phone through Tailscale. To use it away
from the desk, you need a Tailscale account, a tailnet with MagicDNS and HTTPS
certificates turned on in an admin console, a second userspace node just for
Needful, a login link, and the Tailscale app on the phone. That's fine for
someone who already lives on a tailnet. For everyone else, it's a wall between
"I installed Needful" and "it's on my phone."

The same wall stands in front of every small personal app on an Omarchy
desktop that wants a phone view. Each would have to repeat the Tailscale
instructions or invent its own way in. There's no simple, private way to say
"show this app on my phone" and have it work at home, on cellular, and on
someone else's Wi-Fi.

## Appetite

Two cycles. Scope covers a hosted registry for phone pages, a desktop link
that tunnels phone requests to the app, QR pairing, and Needful as the first
and only app using it. No native mobile apps, and no changes to what Needful's
phone page can do.

## Solution

### The pieces, as a user sees them

- **The phone page lives at its own address.** Each app on each desktop gets a
  stable web address, like `needful-k3x9q2m7wd4hfp8a.<domain>`. The address is
  derived from the desktop's own key, so nobody else can claim it. Each app on
  each desktop is its own website, which gives it its own home-screen icon,
  storage and sign-in, separate from every other app.
- **The desktop publishes the page there.** The app uploads its phone page
  (HTML, scripts, icons, manifest) to the registry, signed with the desktop's
  key. The registry only serves what the desktop signed, and keeps a public
  list of every release a site has ever published.
- **Requests go straight to the desktop.** Once installed, the page talks to
  the app over an encrypted link addressed by the desktop's key: directly when
  the networks allow it, through a relay when they don't. The registry never
  sees the app's data; it only hosts the page.
- **Pairing is a QR code.** No accounts and no passwords. A phone is trusted
  because it scanned a code shown on the desktop, and it can be revoked.

### Pairing breadboard

```
Desktop terminal                         Phone
────────────────                         ─────
$ needful serve pair
  [QR code]
  https://needful-k3x9….<domain>/#…
  Desktop fingerprint: 4F2A-91C0-7B3E
  Waiting for a phone…
                                         Scan QR → page opens
                                         "Pair with this desktop?"
                                         Desktop fingerprint: 4F2A-91C0-7B3E
                                         [ Pair ]
  Paired: iPhone, Safari                 → Needful, Today view
                                         "Add to Home Screen for an app icon"
```

The pairing code works once and expires after a few minutes. After pairing,
the phone remembers the desktop's key and refuses any other.

### Commands

`needful serve` keeps its current shape. The link replaces Tailscale as the
default way to reach the phone:

```
needful serve install            Keep serving and the link running as user services
needful serve pair               Show a QR code to pair a phone
needful serve phones             List paired phones
needful serve unpair <phone>     Revoke a phone
needful serve status             Serving, link state, published release
```

```
$ needful serve status
Serving        http://127.0.0.1:8787/
Link           online (direct to 1 phone, relayed to 0)
Phone page     https://needful-k3x9q2m7wd4hfp8a.<domain>/
Release        2026-11-19 14:02, 9 files, signed 4F2A-91C0-7B3E
Check          the registry serves exactly this release ✓
Phones         iPhone (Safari), paired 2026-11-19
```

Publishing happens automatically: when the link starts and the installed
Needful's phone page differs from the published release, it publishes the new
one. There's no separate publish step to forget.

### Trusting the hosted page

The page comes from a hosted registry, so the user should be able to tell
whether it's serving what their desktop signed.

- The desktop periodically fetches its own published page and compares it
  with the release it signed. `serve status` shows the result, and a mismatch
  raises a desktop notification.
- When a paired phone connects, it reports which release it actually loaded.
  If that isn't the release the desktop signed, the desktop warns, and so
  does the phone.
- Every site's release history is public, so anyone can check what was
  published and when.

### The registry

A small hosted service, run like any static host:

- Accepts signed releases from desktops, with no accounts. The first release
  from a key claims that key's addresses.
- Serves each site from its own subdomain on a dedicated domain, kept separate
  from rails.quest.
- Rolls back by republishing an earlier release.
- Enforces per-site size limits and rate limits on uploads.
- Handles abuse like any host: a report link on the main domain, and the
  operator can take a site down.

### For other apps

The link and the registry are general, and Needful is just the first app. Any
desktop app with a phone page can publish to the registry and tunnel through
the link. Needful's integration should be what another app's author would
copy.

## Rabbit Holes

- **Writing our own NAT traversal.** Hole-punching, relays and dialing by key
  are a solved problem. Use an existing library that does them (iroh is the
  front-runner) and spend the time on pairing and publishing.
- **Fighting the browser for code pinning.** Browsers update service workers
  on their own schedule, and a page can't vouch for itself. Don't try to make
  the phone refuse an updated page. Detection (desktop monitoring plus the
  phone reporting what it loaded) is the bar for this pitch.
- **Two service workers on one origin.** Needful's phone page already has a
  service worker, and the link's client needs to intercept requests too. Only
  one service worker can control an origin, so make the link a piece the app's
  own worker includes. Don't design a framework that wraps the app.
- **Cookies through the tunnel.** Responses that arrive over the link can't
  set real browser cookies. Needful's API doesn't depend on cookies, so don't
  build a cookie jar for apps that don't exist yet.
- **Waiting on the Public Suffix List.** Getting the domain listed takes weeks
  and isn't in our control. Submit early, but don't block shipping on it:
  per-subdomain origins already isolate storage and service workers.
- **Wildcard TLS.** Per-host certificate challenges don't work for arbitrary
  subdomains. If wildcard DNS-challenge setup turns into a fight with the
  hosting provider, stop and reshape rather than burning the cycle on
  infrastructure.

## No Gos

- No iOS or Android app. The phone side is the browser and an installed web
  app.
- No change to what Needful's phone page can do: same views, same actions.
- No offline use. If the desktop is asleep or unreachable, the page says so
  and waits.
- No push notifications to the phone.
- No accounts, dashboard or web UI for the registry. Desktops publish through
  the command line, and an operator takes sites down by hand.
- Doesn't run our own relay. The link uses public relays this time.
- Doesn't remove the Tailscale option. `--tailscale` keeps working, but the
  README leads with pairing.
- No sharing a desktop's app with another person. Pairing is for your own
  phones.
