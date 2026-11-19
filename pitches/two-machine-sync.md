---
status: pitched
pitched: 2026-11-19
started: ~
completed: ~
audited: ~
slices: ~
---

# Two-machine sync: the same Needful on the laptop and the desktop

## Problem

Needful only works if it's the one place you trust. Once you use two
computers, it no longer is. If you do the oil change and mark it done on the
laptop, the desktop still shows it as overdue. A note you log at the desk
doesn't follow you to the laptop. Today's list on one machine doesn't match
the other.

The obvious workaround is to put the data file in a sync folder, and it
quietly fails in exactly the case that matters. Mark something done on the
laptop with no network, snooze something else on the desktop, and when the
two machines meet, the sync tool keeps one version and sets the other aside
as a conflict copy. One machine's changes vanish from the app without a
word. New tasks made on both sides before a sync can also end up treated as
the same task. So the person who switches machines all day either sticks to
one machine for Needful or stops trusting the list. Either way, Needful
never becomes the place where nothing is forgotten.

## Appetite

One cycle. Scope covers syncing between your own computers through a folder
that a sync tool you already use keeps in step (Syncthing, Dropbox, iCloud
Drive, Nextcloud). It also covers merging changes made on both machines,
moving existing data over, and showing in the app that sync is working. No
hosted service, no accounts, and no changes to what a task, routine, or
context can do.

## Solution

### What changes for the user

- **Point Needful at a folder, once per machine.** On each computer, run
  setup with the folder your sync tool shares. The first machine moves its
  existing data in. The second sees the data already there and joins it, and
  anything it had on its own is merged in rather than thrown away.
- **Both machines are fully working, always.** Every view, key, and command
  works the same on either machine, online or not. Nothing waits on the
  network and nothing needs the other machine to be awake.
- **Changes show up on their own.** When the sync tool brings in the other
  machine's changes, the open window updates in place, just as it does today
  when the command line changes something.
- **Changes from both machines merge; neither machine's changes are thrown
  away.** Each machine writes only its own record of what it changed, so the
  sync tool never sees two machines writing the same file and never makes a
  conflict copy. Every machine builds the same list from all the records.

### How conflicts resolve

| Both machines… | Result |
| --- | --- |
| changed different things | Both changes kept |
| edited the same field of the same task | The later edit wins |
| marked the same task done the same day | One completion, both notes kept |
| marked it done on different days | Both completions in history; the due date follows the later one |
| one deleted a task, the other edited it | It stays deleted; its history stays too |
| logged notes on a context | All notes kept, in time order |
| pulled things into Today | Today is the union; it still starts clean each morning |

"Later" means when the change was made, not when it arrived. A machine that
syncs a day late doesn't overwrite newer work with older work.

### Command line

```
$ needful sync setup ~/Sync/needful
Moved 142 tasks, 9 contexts, and 1,204 history entries into ~/Sync/needful.
This machine is "desktop". Run the same command on your other computers.

$ needful sync setup ~/Sync/needful        # on the laptop
Joined ~/Sync/needful (from "desktop"). Merged 3 tasks that were only here.
This machine is "laptop".

$ needful sync status
Folder   ~/Sync/needful
This     laptop
desktop  last change 4 minutes ago
laptop   last change just now

$ needful sync status --json
{"folder": "...", "this": "laptop", "machines": [...]}
```

`needful sync rename <name>` changes how this machine is shown. Without
setup, Needful keeps working exactly as it does now, on one file.

### In the window

```
Sidebar footer
──────────────
  ⇄ Synced · desktop 4 min ago
```

The footer stays quiet when all is well. If another machine hasn't been
heard from in a long while, it says so in plain words ("desktop: nothing
since Tuesday"). It never shows a sync tool's internals. It doesn't claim
to know whether the sync tool is running, only when it last saw each
machine's changes.

### What keeps working

- **Undo and redo** undo this machine's own last change, as now.
- **The phone page** (`needful serve`) shows everything from every machine,
  whichever computer runs it. Changes made on the phone count as changes from
  that computer.
- **The command line and coding-agent skill** work against the synced data
  with no new flags.
- **Backups:** you can still copy the folder to keep a full copy.

## Rabbit Holes

- **Running our own sync server or peer-to-peer link.** Moving the bytes is
  the sync tool's job, and it already handles discovery, encryption, and
  offline catch-up. Needful's job is making the data safe to merge. If no
  folder-sync tool is acceptable, that's a separate pitch, and it fits better
  next to the phone-link work.
- **Merging edits inside a note's text.** Two people editing one paragraph at
  once is a document-editor problem. For notes and titles, the later edit
  wins.
- **Fully trusting each machine's clock.** A laptop whose clock is wrong could
  "win" every conflict. Make sure a bad clock can't get the order of one
  machine's own changes wrong, and that order is decided the same way on
  every machine. Don't try to fix clocks.
- **Letting the folder grow forever with no plan.** Years of changes from two
  machines are still small. Folding old changes into a snapshot is fine to
  add if needed, but any snapshot must be written so that two machines never
  write it at the same time. Otherwise it brings back the conflict copies
  this pitch exists to remove.
- **Reading the sync tool's conflict copies.** Once Needful never writes a
  shared file, there's nothing to reconcile. Don't add handling for
  Syncthing's `.sync-conflict` or Dropbox's "conflicted copy" files. If they
  show up, something else is wrong.
- **Phone link coupling.** Phone link assumes "the desktop" holds the data.
  Don't redesign phone link here. Just don't do anything that ties the data
  to one machine.

## No Gos

- Does not sync between different people or households. This is one person's
  own computers.
- Does not move the bytes itself, and does not install, configure, or check
  on a sync tool.
- Does not encrypt the folder beyond what the sync tool already does.
- Does not show a per-change history of which machine did what, beyond the
  last-heard time for each machine.
- Does not offer a way to unsync or split the data back into one file this
  cycle. You can still copy the folder to keep a backup.
- Does not change the phone page's features or how it reaches the phone.

## Stretch / Follow-up

Build this only if the core lands with time to spare. Otherwise it's the
first follow-up.

- **Retire a machine.** An old laptop's changes stay in the folder forever,
  and status keeps saying "nothing since March." `needful sync forget
  <machine>` takes it out of status and the sidebar footer. Everything it
  ever changed stays: forgetting a machine never removes a task, completion,
  or note.

  ```
  $ needful sync forget old-thinkpad
  Forgot "old-thinkpad" (last change 2026-03-02). Its 3,410 changes stay.

  $ needful sync forget laptop
  "laptop" is this machine. Run it from another computer.
  ```

  If a forgotten machine comes back and makes changes, it shows up in status
  again and its changes merge as usual. Forgetting hides a machine from
  status; it doesn't block it.
