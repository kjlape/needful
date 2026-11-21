---
status: pitched
pitched: 2026-11-21
started: ~
completed: ~
audited: ~
slices: ~
---

# Blocked tasks: wait on what you found, then come back

## Problem

You rarely know everything about a task when you add it. Routines are the
exception; you've done them a hundred times. With everything else, you find
out what's in the way once you start: "Replace the cabin filter" turns out to
need a filter you don't have, and "Send the invoice" needs a number from the
client first.

Today Needful can't say "not until that's done." The choices are all bad:

- **Leave it.** It keeps climbing the needful list, late and nagging, while
  there's nothing you can do about it.
- **Snooze it.** That's a guess at a date. Guess too early and it comes back
  still blocked. Guess too late and it sits hidden after the blocker is done.
- **Delete it and remember.** That's the forgetting Needful exists to
  prevent.

The blocker also tends to go untracked, because it came up in the middle of
something else.

## Appetite

One small cycle. Scope covers one blocker per task, made on the spot or picked
from existing tasks, plus the Blocked state across the app, the CLI, the phone
page, and sync. No dependency graphs, no project views, and no "blocks"
lists on the blocker's side beyond a line in its detail.

## Solution

### The idea

A blocked task works like a snoozed one, except it waits for another task
instead of a date. When the blocker is finished, the blocked task comes back
on its own, right where its rank says it belongs.

### The pieces

- **Blocked by.** A task gets one `blockedBy`, the id of another task. While
  it's set, the task is in a new **Blocked** state next to Snoozed and Out of
  season. It leaves Needful and Today and shows in Upcoming as
  "Waiting on: *Buy a cabin filter*".
- **Make the blocker on the spot.** `B` on a task opens a quick add for a
  one-time task in the same context, already filled in as its blocker. This is
  the common case, because you find blockers while you work. From the same
  dialog you can also pick an existing task instead.
- **Unblock when it's done.** The blocker reaches a terminal state when it's
  **done** (any completion, so a recurring blocker counts too) or **deleted**.
  The blocked task's `blockedBy` clears and it's active again.
- **A skip doesn't unblock.** A skip of the blocker means the work didn't
  happen, so the task stays blocked and the blocker moves on to its next time.
- **Waiting isn't late.** Time spent blocked doesn't count toward lateness,
  just like time out of season. A task that waited three weeks on parts comes
  back where it was, not at the top for having waited.
- **One blocker, and chains.** If a second blocker turns up, block the blocker.
  "Replace the cabin filter" waits on "Buy a cabin filter", which waits on
  "Look up the filter size". Each step comes back when the one before it is
  done.
- **Per copy.** A routine copy is blocked alone: the Tacoma's oil change can
  wait on parts while the CR-V's goes ahead, the same as snooze.
- **Unblock by hand.** `B` on a blocked task offers "Stop waiting", for when
  the blocker turns out not to matter.

### Breadboard

```
Needful list                         B on "Replace the cabin filter"
────────────                         ───────────────────────────────
▸ Replace the cabin filter    CR-V   Blocked by…
  Mow the lawn                Home     New: [Buy a cabin filter      ]
  Send the invoice          Client     in CR-V, once
                                       — or pick a task: [ search… ]
                                     [ Block ]

After:                               Upcoming
  Buy a cabin filter          CR-V     Replace the cabin filter   CR-V
  Mow the lawn                Home       Waiting on: Buy a cabin filter
  Send the invoice          Client

Space on "Buy a cabin filter" → done → "Replace the cabin filter" is back
in the needful list.
```

The blocker's detail shows "Blocking: Replace the cabin filter" so it's clear
why it's there.

### Command line

```sh
needful block 3 --on 7                     # wait on an existing task
needful block 3 --new "Buy a cabin filter" # make the blocker, same context
needful unblock 3
needful list --view upcoming               # blocked tasks show what they wait on
```

`--json` output gains `blockedBy` and a `blocked` state.

## Rabbit Holes

- **Lateness accounting.** "Blocked time doesn't count" needs a record of when
  the block started. Store `blockedSince` and treat the span the way out of
  season is treated: push the due date forward by the blocked span when the
  task unblocks. Don't build a general history of state changes.
- **Cycles.** Blocking A on B when B already waits (directly or down the
  chain) on A has to be refused at the point of blocking, in the store, so the
  CLI, the app, and sync all get the same rule.
- **Sync.** The blocker can be finished or deleted on the other machine. The
  unblock has to follow from the merged data, not from a local event: on load
  and after a merge, any `blockedBy` pointing at a completed-since-blocked or
  missing task clears. That one rule also covers deletes, undo, and old files.
- **Undo.** Undoing the blocker's completion should block the task again.
  Unblocking derived from the data, as above, gets this for free.
- **Routine blockers.** If the blocker is a routine copy, any completion of
  *that copy* counts. Completions of the routine's other copies don't.

## No Gos

- More than one blocker per task. Chains cover it.
- A dependency graph view, Gantt charts, or project planning.
- Blocking on things that aren't tasks (dates, people, outside events).
  Snooze covers dates; a one-time task covers the rest ("Hear back from the client").
- Notifications when something unblocks. It shows up in the list, ranked,
  like everything else.
