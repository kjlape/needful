---
status: completed
pitched: 2026-11-24
started: 2026-11-24
completed: 2026-11-24
audited: ~
slices: ~
---

# Shared universes: share one list with someone, not all of them

## Problem

Universes were made for other people's lists: a partner's, the house's. But
peer sync can't share a list with another person without sharing everything.
It treats every computer you pair with as one of your own. That computer gets
every universe you have, joining it merges your own list into its owner's,
and every one of your computers starts trusting it.

So the households and working arrangements universes were meant for can't be
set up without a separate sync tool:

- a **family** list that everyone in the house sees,
- a **personal** list kept on your computers and your spouse's, and no one
  else's,
- a **client** list shared with one client's laptop, which must never see the
  family list, your personal list, or your own tasks.

Today, pairing the client's laptop for the client list hands them all of it.
The only way to keep lists apart is to give each one a folder and run
Syncthing or Dropbox per person, which is the setup peer sync exists to
spare people.

## Appetite

Small batch, two to four days. Scope covers telling your own computers apart
from people you share with, sharing a single universe by code, sending each
universe only to the people in it, and seeing and removing who's in a
universe. Folder sync, how changes merge, and the phone page are unchanged.

## Solution

### Two kinds of pairing

- **Your computers**, paired as today with `needful sync pair` / `sync join`.
  Nothing changes for them: they share your own list and every universe,
  pairing one trusts it on all of them, and unpairing it on one unpairs it
  everywhere.
- **People you share with**, added from a single universe. They get that
  universe and nothing else. They aren't added to your computers' trust
  list, so your client never learns about your wife's laptop. Your own list
  is never merged with theirs.

Everyone already paired counts as one of your computers, so today's setups
keep working with nothing to change.

### Sharing a universe

```
$ needful universe share family
Share "family" with code 4-amber-lantern-quiet
This machine: "desktop"  4F2A-91C0-7B3E
Waiting… (the code works once, for a few minutes)
```

```
$ needful universe join 4-amber-lantern-quiet
Join "family" from "desktop"  4F2A-91C0-7B3E? [y/N] y
Joined "family" as a new universe. 12 tasks.
```

The person joining gets a new universe beside their own list. It takes the
shared universe's name, or name-2 if they already have one by that name.
Their own list, their other universes, and their computers stay where they
are. Your other computers get the new member along with the universe itself,
so any of them can sync it with that person afterwards.

The same codes and fingerprint check as computer pairing, and the same
failure cases: a mistyped, used, or expired code shares nothing.

In the window, right-clicking a universe gives **Share…** (shows the code)
and **People…** (who's in it). The sidebar's ⋯ menu gains **Join a shared
universe…** next to Join a computer.

### Who's in a universe

```
$ needful universe members family
family
  Your computers   desktop, laptop
  wife-laptop      C81D-0A47-E2F9   connected
  wife-desktop     7B02-E4A1-90DD   via wife-laptop
  kid-ipad-mini    19FF-3C20-AB71   last seen 3 days ago

$ needful universe unshare family kid-ipad-mini
Removed "kid-ipad-mini" from "family". It keeps what it already has.
```

Anyone in a universe can share it further and remove people from it. This
follows the existing rule that anyone in a universe can change anything in
it. A removal reaches the rest of the universe the same way changes do.

### A member's own computers

When your wife joins the family universe on her laptop, her laptop shares it
with her other computers, the way it shares any of her universes. Her
desktop gets the family list through her laptop without pairing with you,
and it shows in the members list as reached through her laptop. Sharing a
universe with a person therefore means sharing it with all of their
computers.

### Breadboard

```
Universe (right-click)
  Share…   → code + fingerprint, waiting
               → joined: "wife-laptop joined family"
               → expired / cancelled: nothing shared
  People…  → list of your computers + members, each with Remove
               → Remove → confirm "keeps what it already has" → gone

⋯ menu
  Join a shared universe… → enter code → confirm name + fingerprint
                              → new universe in sidebar, selected
```

## Rabbit Holes

- **Per-person permissions** (read-only members, only the owner can share).
  Tempting for the client case, but there's no owner today and nothing to
  enforce it with once someone has the files. Stop at all-or-nothing
  membership. If a client must only read, that's a different feature.
- **Taking back what a removed member has.** It can't be done. Removing
  someone stops future changes only, and the UI says so. Don't build
  "remote wipe".
- **Passing your computers' trust list to members.** Reusing the existing
  device-list sharing for members is the quickest route, but then a client
  learns about every computer you own, and older Needfuls would start
  trusting the client for everything. People you share with must live
  where older versions don't read them.
- **Older Needfuls on your own computers.** A computer still on an old
  version offers every list to everyone it trusts. Since members aren't in
  your shared trust list it won't trust them. Still, check that a not-yet-
  upgraded computer can't relay a universe to someone outside it, and say
  in the release notes that all your own computers should be upgraded
  before you share.
- **Merging a joined universe with one you already have.** If the joiner
  already has a "family" from a separate folder setup, don't try to
  detect it and merge. Join it as a new universe. Combining two is a
  separate problem.

## No Gos

- Does not change folder sync. Universes in a Syncthing or Dropbox folder
  keep syncing as they do, alongside any members.
- Does not share your own list ("mine") with anyone but your own computers.
- Does not offer read-only or limited members.
- Does not remove a list, or anything in it, from a removed member's
  computers.
- Does not let a member see which other universes you have, or who's in
  them.
- Does not add sharing to the phone page. Phones stay tied to the computer
  that serves them.
- Does not hide one person's changes from another inside a shared universe.
