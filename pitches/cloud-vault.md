---
status: pitched
pitched: 2026-11-21
started: ~
completed: ~
audited: ~
slices: ~
---

# Cloud vault: an always-on computer you don't have to own, that can't read your list

## Problem

Peer sync only moves changes while two of your computers are awake at once.
The README's answer is to "run `sync install` on one always-on computer, like
a desktop." Plenty of people don't have one. They have a laptop and a work
machine, or a laptop and a Mac mini that sleeps. They tick off the oil change
on the laptop on Saturday, open the other machine on Monday, and it's still
due, because the two were never on at the same time.

The other ways around this bring back what peer sync removed. A sync folder
in Dropbox or iCloud means an account, with another company holding a list
it can read. Syncthing or rsync means another program to set up and keep
running. Renting a server and running Needful on it means a Linux box to
look after, and the list sits there in plain text.

What these people want is an always-on computer of their own that never
sleeps, costs them no upkeep, and can't read what it holds.

## Appetite

One cycle. Scope covers a vault (an always-on peer that only ever holds your
list locked), pairing your computers with one, locking and unlocking on your
own computers, and a hosted vault that Needful users can opt into, which
anyone can also run themselves. No accounts, no billing, and no change to how
changes merge.

## Solution

### The pieces, as a user sees them

- **A vault is a computer that's always on and can't read your list.** It
  pairs like one of your computers, shows up in `sync status`, and takes and
  hands out changes whenever any of your computers is awake. The laptop's
  Saturday change waits in the vault until Monday, when the other machine
  picks it up.
- **Your computers lock everything before it reaches the vault.** Your lists,
  and the names of your universes, leave your computer already locked with a
  key that only your paired computers hold. The vault stores what it's given
  and hands it back. It never gets the key, so it can't open your lists, and
  it can't quietly change one without your computers noticing.
- **Your computers still talk to each other directly.** Nothing about two of
  your computers syncing at home changes. The vault is one more peer. It
  helps only when no two of your computers are on together.
- **Opt in, from the window or the command line.** Without a vault, Needful
  works exactly as it does today, with nothing hosted. Turning one on is a
  single step, and turning it off deletes what it held.
- **The hosted vault is the easy default, not the only one.** Needful offers
  one hosted vault, at one address, for anyone who wants it. Anyone can run
  the same vault on a server they own with one command, and point their
  computers at it instead, the way `sync relay` works today.
- **New computers get the key when they pair.** Pairing a computer already
  checks a code and a fingerprint. The new computer gets the key from the
  computer it pairs with, at that moment. The vault is never part of it.
- **Unpairing changes the lock.** Unpair a computer and your others pick a
  new key and lock everything in the vault again with it. The old computer
  can't open anything added after that, and the vault turns it away.

### Command line

```
$ needful sync vault public
Your lists will also be kept, locked, at vault.<domain>.
It holds them for when none of your computers are on, and can't read them.
Locking 3 lists… done.

$ needful sync vault https://vault.example.net
Your lists will also be kept, locked, at vault.example.net.

$ needful sync vault off
Asked the vault to delete what it held. Your computers keep everything.

$ needful sync status
This     laptop  C81D-0A47-E2F9
vault    vault.<domain> · up to date · holding 3 lists, locked
desktop  not reachable since Saturday · last change 2026-11-15
```

For someone running their own:

```
$ needful vault serve --at vault.example.net
Vault listening at vault.example.net, port 7787.
It holds locked lists and can't read them.
```

Unpairing says what happened to the lock:

```
$ needful sync unpair old-mac
Unpaired "old-mac". It can no longer connect. Its changes stay.
Changed the vault's lock, so old-mac can't open anything added from now on.
```

### In the window

The sidebar's ⋯ menu gets "Keep in a vault…" next to Pair a computer. It
offers the hosted vault, a vault you name, or none, with a sentence on what
the vault can and can't see. When the vault is all that's carrying your
changes, the sidebar footer says so:

```
  ⇄ vault · up to date · desktop not on since Saturday
```

### What the vault can and can't see, said plainly

The README and the "Keep in a vault…" dialog say both sides:

- **Can't see:** any task, context, note, history, step, or universe name.
- **Can see:** that you have some lists, roughly how big each is, how many
  computers you have, when each one last changed something, and from what
  internet address.
- **Can't do:** change a list, add a task, or pair a computer of its own.
- **Could do:** refuse to hand things back, or hand back an older copy. Your
  computers never step back to something older than they've already seen,
  but a computer joining for the first time while the vault is misbehaving
  could start from an old copy until one of your other computers comes on.

## Rabbit Holes

- **Accounts, sign-ups and billing.** A hosted vault invites a login page,
  plans and payment. Your paired computers already prove who you are, so the
  hosted vault knows you as "these paired computers" and nothing else. Hold it
  to a size limit per set of computers that a to-do list never gets near.
  Paying for it is a separate decision.
- **Making the vault a backup you can restore with nothing.** "I lost every
  computer, give me my list back" is the obvious next request, and the vault
  can't answer it: the key only lives on your computers. A recovery code to
  write down is the likely follow-up, but it changes what losing that paper
  means. Leave it for its own pitch, and say in the dialog that the vault
  isn't a backup.
- **Locking traffic between your own computers too.** The link between your
  computers is already encrypted end to end, and they're meant to read each
  other's lists. Lock only what goes to the vault.
- **Having the vault serve the phone page.** It's tempting to see the vault as
  the always-on Needful phone link needs. It can't be, because it can't read
  the list. The phone would have to hold the key and do the work itself,
  which is the "phone as a full peer" path phone link turned down. Keep the
  two separate this cycle. If phone link's need for an always-on computer
  proves to be the bigger wall, reshape both together, not halfway in here.
- **Hiding the remaining clues.** Padding files to fixed sizes, faking
  traffic, or routing through other servers to hide when you're active are
  real privacy work with real costs. Say what the vault can see, plainly,
  instead.
- **Clever locking schemes.** Separate keys per universe, per computer, or
  for shared lists between people all look natural, and every one of them
  makes pairing and unpairing harder to get right. Use one key for everything
  your paired computers share, and change it on unpair.
- **Changing the merge.** The vault holds the same files your computers
  already pass around, just locked. If it seems to need a merge change,
  something is wrong with how the vault was wired in.

## No Gos

- No accounts, passwords, email sign-up or payment.
- No vault unless you turn it on. Needful with no vault uses nothing hosted,
  as today.
- Does not let anyone, Needful's host included, read your lists from the
  vault.
- Does not restore your list if every one of your computers is lost.
- Does not serve the phone page or let a phone read through the vault.
- Does not share lists between people or households.
- Does not hide when you're active or how big your lists are from the vault.
- Does not change how computers sync with each other directly, or how
  changes merge.
- Does not remove or change folder sync or the relay.
