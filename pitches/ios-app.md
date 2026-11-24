---
status: pitched
pitched: 2026-11-22
started: ~
completed: ~
audited: ~
slices: ~
---

# iPhone app: your own copy of Needful in your pocket, that works with no signal

## Problem

The routine things Needful tracks mostly happen away from the desk: in the
garage at the car, in the yard, at the hardware store looking at air filters.
That's where you want to tick off the oil change with a note on what went in,
or check when the filter was last swapped.

Today the phone page is the only way to do that, and it's a window onto a
Needful running on some other computer. It only works while that computer is
on and reachable. In a garage with one bar of signal, in a basement, or on a
plane, the page says it can't reach your Needful and waits. Whatever you did
in the meantime you have to remember and enter later, and Needful exists so
you don't have to remember.

Getting the page onto the phone at all also takes Tailscale today (or, once
phone link lands, a hosted page you have to trust). Someone who has paired a
laptop and a desktop in two minutes with a code reasonably expects the phone
to pair the same way and then just be one of their devices.

## Appetite

Two cycles. Scope covers an iPhone app built from the same code as the
desktop app, with a phone-shaped window onto the same lists, its own copy
of your list, and pairing and sync with your computers the way computers
already pair. It ships through TestFlight. No iPad layout, no Android, and
nothing the desktop window can't already do.

## Solution

### The pieces, as a user sees them

- **The phone is one of your devices.** It pairs with your computers by the
  same code `sync pair` shows, and from then on it keeps its own copy of
  every list, the same as the laptop does. Ticking off a task with no signal
  works, and the change goes out the next time the phone and one of your
  computers can reach each other.
- **It syncs while it's open.** Opening the app, or bringing it back to the
  screen, connects to whichever of your computers it can reach and catches
  up both ways. Changes made on the phone go out as soon as there's a
  connection. `sync install` on an always-on desktop (or a vault, once that
  lands) means the phone always has somewhere to catch up from.
- **It carries changes like any computer does.** A change made on the laptop
  that reached the phone gets passed on to the desktop the next time the
  phone sees it, even if the laptop is closed by then.
- **Computers don't fret about it.** Phones are away most of the time. In
  `sync status` and the window, a phone shows as a phone and when it last
  checked in, and isn't flagged as "not reachable."
- **Same lists, same rules.** Needfulness, contexts, routines, today,
  snooze, schedules, seasons, history, notes, checklists, and universes all
  behave on the phone exactly as they do on the desktop, because the phone
  runs the same Needful underneath.
- **It follows the phone's light and dark mode,** in place of the desktop
  theme.

### Pairing breadboard

```
Desktop terminal (or ⋯ › Pair a computer)       iPhone
─────────────────────────────────────────       ──────
$ needful sync pair
  [QR code]
  Pair code: 7-violet-crumble-tango.<…>
  This machine: "desktop"  4F2A-91C0-7B3E
  Waiting… (works once, for a few minutes)
                                                 First open: "Pair with a computer"
                                                 [ Scan code ]  [ Paste code ]
                                                 → camera, scan QR
                                                 "Pair with desktop  4F2A-91C0-7B3E?"
                                                 [ Pair ]
  Paired with "iPhone"  9B07-…                    Joined its list.
                                                 → Today
```

`sync pair` gains a QR code beside the code it prints, so the long code never
has to be typed on a phone. The window's Pair dialog shows the same QR code.
Needful asks once for local network access, saying it's to find your
computers at home; saying no still works through the relay.

On the phone, settings hold the same steps as the sidebar's ⋯ menu: paired
computers with their last change, Pair, Unpair, Rename this phone, and the
relay choice the computers made (read-only).

### The phone's window

```
┌──────────────────────────────┐
│ Mine ▾       Home ▾      🔍  │   universe and context pickers, search
├──────────────────────────────┤
│ Mow the lawn          ●●● 2w │   rows as on the desktop, bigger
│ Air filter            ●●  5w │
│ Invoice Acme          ●●● 1d │   swipe right → Done (note sheet)
│ ...                          │   swipe left  → Snooze / Today
│                              │   tap → detail; edit from there
│                          (+) │   new task
├──────────────────────────────┤
│ Today  Needful  Upcoming  History │
└──────────────────────────────┘
```

- **Tabs** replace `1`–`4`. The four lists are the same lists.
- **Pickers** at the top replace `[` `]`, `0`, and `U`.
- **Swipes** replace `Space`, `T`, and `S`. Every swipe can also be done
  from the task's detail, so nothing depends on finding a gesture.
- **Sheets** replace the dialogs: done with a note, snooze, the editor,
  log a note (with follow-up), new context.
- **Undo** appears for a few seconds after each change, in place of
  `Ctrl+Z`.

### Getting it

The app goes out through TestFlight, built by one script beside
`bin/build-macos`, with the version read from the same place as every other
build. Installing it is opening a TestFlight link.

## Rabbit Holes

- **Rewriting the window in SwiftUI.** It would feel more native, but it
  means a second copy of ranking, schedules, merging, and pairing, kept in
  step forever. The phone runs the same Needful underneath; only the window
  is new. Use native pieces (camera, share sheet) where Qt's are missing,
  not as a reason to fork.
- **Squeezing the desktop window onto the phone.** The desktop layout, its
  sidebar, and its keyboard-first design don't shrink into a phone. The
  phone gets its own top-level screens and sheets. Reuse the rows and
  pieces that fit; don't add breakpoints to the desktop window to make one
  layout serve both.
- **Syncing in the background.** iOS gives apps little and unpredictable
  time when they're off screen, and keeping the link alive there means
  background modes, wake-ups, and review questions. Catch up on open and on
  change. If one connected burst can't finish before the app is put away,
  let it finish next time; don't build background sync.
- **The link library on iOS.** The link is a Rust library built for desktops,
  and it has to cross-compile for iPhone and live inside an app that iOS
  suspends at will. Prove a phone can pair and pass one change with a
  desktop in the first week. If iroh or its local-network discovery won't run
  on iOS without deep surgery, stop and reshape (relay-only, or a narrower
  first version) rather than patching the library.
- **A sync folder on the phone.** The desktop also syncs through a folder,
  and iCloud Drive looks like the obvious phone answer. It needs iCloud
  entitlements, file coordination, and handling files that haven't
  downloaded, and it reintroduces an account the peer link was built to
  avoid. The phone syncs only by pairing.
- **The App Store.** Listing it brings review, screenshots, a privacy
  label, and Qt's licence terms for statically linked apps. TestFlight
  proves the app first. Treat the listing as its own pitch.
- **Treating the phone page and the app as one thing.** The app makes the
  phone page less needed, but don't fold it into the app or retire it.
  People without an iPhone, or who don't want an app, still use the page.

## No Gos

- No iPad layout. On an iPad it runs as the iPhone app.
- No Android app.
- No App Store listing. It ships through TestFlight.
- No notifications, widgets, Siri, Shortcuts, or Apple Watch.
- No syncing while the app is off screen.
- No sync folder on the phone. It syncs only with paired devices.
- No command line, `serve`, phone page, or `skill install` on the phone.
- Doesn't host a phone page or act as an always-on peer for your computers.
- No features the desktop doesn't have. The phone does what the desktop
  window does.
