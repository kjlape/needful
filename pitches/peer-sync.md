---
status: pitched
pitched: 2026-11-19
started: ~
completed: ~
audited: ~
slices: ~
---

# Peer sync: your computers keep each other in step, with nothing in between

## Problem

Two-machine sync works, but only if you already run a sync tool: Dropbox or
iCloud (an account with someone else holding your list), Syncthing (another
program to install, configure and keep running), or rsync on a timer you
wrote yourself. For someone who just wants the laptop and the desktop to
agree, each of these is a detour into setting up something that isn't
Needful. For someone who wants no outside company in the loop at all, the
easy options are ruled out and the rest are chores.

A second gap is coming. Phone link needs a way to say "this phone is mine"
and to talk to it privately. If each feature invents its own pairing, you end
up with two lists of trusted devices, two ways to revoke one, and two places
for it to go wrong.

## Appetite

One cycle. Scope covers pairing your own computers with a code, an encrypted
link between paired computers that works at home and away when they can
reach each other, passing each machine's changes over it, and showing in the
app who is connected. The pairing and the link are built so phone link uses
them as they are. No hosted service, no accounts, and no change to how
changes merge.

## Solution

### What changes for the user

- **Pair once, by code.** On one computer, ask for a pairing code. On the
  other, type it. Both show the same fingerprint so you can check you paired
  with your own machine. From then on, they trust each other and nobody else.
- **Joining works like folder setup.** The joining computer takes in the
  other's list and merges in what only it had, with the same counts and the
  same "nothing is doubled" rule as `sync setup`.
- **They find each other on their own.** On the same network, paired
  computers find each other with nothing to configure. Away from home, they
  connect whenever one can reach the other directly. Nothing outside your own
  machines is used unless you add a relay yourself.
- **Changes travel whenever two are awake.** Whenever any Needful is running
  (the window or the phone page's service), it keeps the link up and swaps
  changes with every paired computer it can reach. Each machine passes along
  everything it has, so with three computers, an always-on one carries the
  laptop's changes to the desktop even if those two are never on at once.
- **Everything else is the same as folder sync.** Both machines are fully
  working offline. Changes merge by the same rules, and the open window
  updates in place. Undo, the phone page, the command line and the agent
  skill don't change.
- **Folder sync keeps working.** A machine can sync through a folder, through
  peers, or both. Moving from a folder to peers loses nothing.

### Pairing breadboard

```
Desktop                                   Laptop
───────                                   ──────
$ needful sync pair
  Pair code: 7-violet-crumble-tango
  This machine: "desktop"  4F2A-91C0-7B3E
  Waiting for a computer… (code works once,
  for a few minutes)
                                          $ needful sync join 7-violet-crumble-tango
                                            Pair with "desktop"  4F2A-91C0-7B3E? [y/N] y
  Paired with "laptop"  C81D-0A47-E2F9      Paired with "desktop".
                                            Joined its list. Merged 3 tasks that were only here.
                                            This machine is "laptop".
```

A code that's mistyped, used twice, or expired fails without pairing
anything. The window has the same flow: a "Pair a computer" item shows the
code and fingerprint, and a "Join" item takes one.

### Command line

```
$ needful sync status
This     laptop  C81D-0A47-E2F9
desktop  connected on this network · last change 4 minutes ago
nas      connected directly · last change just now
old-mac  not reachable since Tuesday · last change 2026-10-02

$ needful sync status --json
{"this": "laptop", "machines": [{"name": "desktop", "link": "local", ...}]}

$ needful sync unpair old-mac
Unpaired "old-mac". It can no longer connect. Its changes stay.

$ needful sync reach desktop desktop.example.net
Will also look for "desktop" at desktop.example.net.

$ needful sync relay https://relay.example.net
Paired computers that can't reach each other will meet through
relay.example.net. It passes along encrypted traffic and can't read it.
```

`needful sync install` keeps the link running as a user service on a machine
where neither the window nor `needful serve` usually runs. `rename` and
`forget` work as they do for folder sync. Unpairing is the stronger step:
the machine can no longer connect, and its past changes stay.

### In the window

```
Sidebar footer
──────────────
  ⇄ desktop · connected · 4 min ago
```

When all is well, the footer stays quiet, as it does for folder sync. If a
paired machine hasn't been reachable in a while, it says so in plain words
("desktop: not reachable since Tuesday"). Pairing, joining and unpairing are
in the sidebar's menu.

### One list of trusted devices, for phone link too

Pairing is a Needful-wide piece, not a sync feature. Each install has one
identity, shown as its fingerprint, and one list of paired devices. Phone
link adds phones to the same list (by QR instead of a typed code) and uses
the same encrypted link to reach the desktop. Unpairing a phone or a computer
is the same step. This cycle ships only computers in that list, but the
pairing and the link must be usable by phone link with no rework: the same
code flow, carried in a QR, pairs a browser.

## Rabbit Holes

- **Writing our own NAT traversal, discovery or encryption.** Dialing by key,
  hole-punching and encrypted transport are solved problems. Use a
  maintained library that does them (iroh is the front-runner, as in phone
  link) and spend the cycle on pairing and on the user's experience. Make
  sure it runs with its default public servers turned off, since "no third
  party" is the point.
- **Porting Dat or Hypercore.** Their model (one writer per log, replicated
  by everyone) is what Needful's files already are, but the maintained code
  is JavaScript and would mean shipping Node. Don't add a runtime to swap a
  few small files.
- **Sending only what changed.** Each machine's file is small. Send whole
  files when the other side's copy is older. Byte-level deltas or a new log
  format are a different, later problem.
- **Changing the merge.** The merge was built so the transport doesn't
  matter. If peer sync seems to need a merge change, something is wrong with
  the transport, not the merge.
- **Running a relay service for everyone.** A public relay is an operations
  commitment with costs and abuse handling, and it brings back the third
  party this pitch removes. Support pointing at a relay you run. Hosting one
  is a separate decision, shared with phone link.
- **Phone pairing UI.** It's tempting to finish the QR flow while the pieces
  are fresh. The browser side has its own problems (service workers, which
  page is trusted) that belong to phone link. Make sure the pairing would
  work from a QR, and stop there.

## No Gos

- Does not sync between different people or households.
- Does not use any hosted service, account, or public server by default.
- Does not run or host a relay. You can point Needful at one you run.
- Does not pair phones this cycle. Phone link does that, using this pairing.
- Does not move changes between two computers that are never awake at the
  same time, unless a third paired computer is.
- Does not remove or change folder sync.
- Does not change how changes merge or what a task, routine or context can
  do.
- Does not show a per-change history of which machine did what.
