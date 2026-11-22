---
status: pitched
pitched: 2026-11-21
started: ~
completed: ~
audited: ~
slices: ~
---

# Phone link: your list on your phone, paired by a QR code, without Tailscale

## Problem

Needful's phone page only reaches the phone through Tailscale. To use it away
from the desk, you need a Tailscale account, a tailnet with MagicDNS and HTTPS
certificates turned on in an admin console, a second userspace node just for
Needful, a login link, and the Tailscale app on the phone. That's fine for
someone who already lives on a tailnet. For everyone else, it's a wall between
"I installed Needful" and "it's on my phone."

Meanwhile, Needful's computers already reach each other at home, on cellular
and across networks, paired by a one-time code, encrypted end to end, through
a relay that can't read what it passes. The phone is the only device left out
of that, and it's the one you carry to the car, the yard and the store.

## Appetite

One cycle. Scope covers one public web client for Needful, pairing a phone to
an always-on Needful with a QR code, and the phone page's current views and
actions working through it. No native mobile apps, and no changes to what the
phone page can do.

## Solution

### The pieces, as a user sees them

- **One web client, at one address, for everyone.** Needful has a public phone
  page at its own address. It's the same page for every user: it holds no
  lists and knows nobody. On its own, it can only ask to be paired.
- **It talks straight to your Needful.** Once paired, the page talks to your
  always-on Needful (the one running `serve` or `sync install`) over the same
  encrypted link your computers use, met at the relay. The host serving the
  page never sees your list, and neither does the relay.
- **Pairing is a QR code.** No accounts and no passwords. A phone is trusted
  because it scanned a code shown on the desktop, and it can be revoked. The
  code rides in the part of the address a browser never sends to the host.
- **A phone is a window, not a copy.** A paired phone reads and changes the
  list on the Needful it paired with. It keeps no list of its own, and it
  isn't one of your computers: it doesn't show up as a machine that's away,
  and nothing waits for it to come back. When that Needful can't be reached,
  the page says so and waits, as it does today.

### Pairing breadboard

```
Desktop terminal                          Phone
────────────────                          ─────
$ needful serve pair
  [QR code]
  https://<domain>/#7-violet-crumble-tango.…
  This machine: "desktop"  4F2A-91C0-7B3E
  Waiting for a phone… (code works once,
  for a few minutes)
                                          Scan QR → page opens
                                          "Pair with desktop?"
                                          Fingerprint: 4F2A-91C0-7B3E
                                          [ Pair ]
  Paired: iPhone, Safari                  → Needful, Today view
                                          "Add to Home Screen for an app icon"
```

After pairing, the phone remembers that Needful's key and refuses any other.
Opening the page again goes straight to the list, with a moment of
"Connecting…" while it finds the desktop.

A phone can pair with more than one Needful, like your universe and hers.
The page lists them by name, and switching is a tap, the way `U` switches
universes in the window.

### Commands

`needful serve` keeps its current shape. Pairing replaces Tailscale as the
way the README leads with:

```
needful serve install            Keep serving and the link running as user services
needful serve pair               Show a QR code to pair a phone
needful serve phones             List paired phones
needful serve unpair <phone>     Revoke a phone
needful serve status             Serving, link state, paired phones
```

```
$ needful serve status
Serving     http://127.0.0.1:8787/
Link        online through the relay
Phone page  https://<domain>/
Phones      iPhone (Safari)   paired 2026-11-21 · last seen 3 minutes ago · page 2026-11-20 a41c…
```

`--universe` works on every command, as it does today. Phones and computers
share one list of trusted devices underneath: `sync status` lists computers,
`serve phones` lists phones, and unpairing either cuts it off at once.

### Old and new

The hosted page and your desktop update on their own schedules. The page
tells the desktop which version it is, and the desktop tells the page what it
can do. A page newer than the desktop hides what that desktop can't do (as
the page already does for an older desktop's log); a desktop too old for
the page gets a plain "update Needful on desktop" rather than broken buttons.

### Trusting the hosted page

Whoever serves the page could, in principle, serve a page that reads your
list once it's paired. This pitch makes that visible and avoidable, not
impossible:

- Each release of the page is built reproducibly from this repository, and
  its fingerprint is published with the release.
- A paired phone reports the release it loaded, and `serve phones` and
  `serve status` show it. A release that isn't a published one raises a
  desktop notification.
- `needful serve` also serves the same page on this machine, so someone who
  doesn't want to trust the hosted copy can keep using it over Tailscale or
  their own network. `--tailscale` keeps working.

## Rabbit Holes

- **Making the phone a full sync peer.** It's tempting to treat the phone as
  one more computer and give it a copy of the list. A browser tab comes and
  goes, gets wiped, and can't stay awake: it would sit in every computer's
  list as "not reachable since Tuesday," and could strand changes that never
  got passed along. Keep the phone on its own kind of connection, asking the
  always-on Needful, and keep it out of change passing entirely.
- **Direct connections from the browser.** Browsers can't send the raw
  traffic hole-punching needs, so the page always goes through the relay. For
  a to-do list that's plenty. Don't chase WebRTC or other browser transports
  to get a direct path this cycle.
- **Sharing the link's code as is.** The link library is built for a desktop
  process (pipes, a C interface, a multi-threaded runtime, local-network
  discovery), and none of that exists in a browser. Pull out the shared
  pieces (pairing, framing, messages) rather than trying to compile the whole
  library for the web. If iroh no longer builds for the browser with the
  features Needful needs, stop and reshape before writing a transport.
- **Loading the app from the desktop.** Having the hosted page fetch its app
  code from your own Needful would keep the two in lockstep, but it means
  running code that arrived over the link, which fights the page's content
  security policy and doesn't remove the need to trust the loader. Version
  negotiation is enough.
- **Fighting the browser for code pinning.** Browsers update service workers
  on their own schedule, and a page can't vouch for itself. Detection (the
  phone reporting what it loaded, the desktop checking it against published
  releases) is the bar for this pitch.
- **Keeping the connection alive in the background.** Phones drop a page's
  connections soon after it leaves the screen. Reconnect when it comes back,
  as the page already refreshes then. Don't build keep-alives or background
  sync.
- **A big download.** The link compiled for the browser will be far heavier
  than today's page. Measure it in the first week. If it's large enough to
  make the first open slow on cellular, keep it cached and show the page's
  shell before the link is ready, rather than trimming the link library.

## No Gos

- No iOS or Android app. The phone side is the browser and an installed web
  app.
- No change to what Needful's phone page can do: same views, same actions.
- No offline use. If the always-on Needful can't be reached, the page says so
  and waits.
- No push notifications to the phone.
- No accounts, no dashboard, and nothing about your list stored by the host.
- Doesn't host pages for other apps. The link stays general, but only
  Needful's page is served.
- Doesn't run our own relay. The link uses the public relay, or the one set
  with `sync relay`.
- Doesn't remove the Tailscale option. `--tailscale` keeps working, but the
  README leads with pairing.
- No sharing a Needful with another person. Pairing is for your own phones.
