---
status: pitched
pitched: 2026-11-21
started: ~
completed: ~
audited: ~
slices: ~
---

# Checklists: the steps inside a task, fresh each time

## Problem

You rarely know everything about a task when you add it. "Replace the cabin
filter" turns out to need a filter you don't have, and "Send the invoice"
needs the hours added up first. Once you know, the only place to put it is
the notes, where nothing tracks it and the needful list still says "Replace
the cabin filter" when the real next thing is "Buy a cabin filter".

Routines have the opposite problem. You do know the steps. An oil change
means oil, filter, resetting the reminder, and writing down the mileage, and
each time you either keep them in your head or retype them into the note.
History can't show that the reminder never got reset last time.

Much of this happens away from the desk. You're at the parts counter, under
the car, or in the hardware store aisle, and that's where you need to see the
steps, tick them off, and add the one you just found. The phone page has to be
a first-class place for checklists, not a read-only view of them.

## Appetite

One small cycle. Scope covers a checklist on any task, its reset for each
time, its snapshot in history, and support in the app, the CLI, the phone
page, and sync. The phone page gets the full checklist (see, check, add, and
finish with boxes left), since that's where most checking happens. No nested
steps, no due dates or priorities on steps, and no
steps as tasks of their own.

## Solution

### The idea

A task can have a short checklist. Checking boxes doesn't change how needful
the task is, but the row shows the next unchecked box, so the list tells you
what to do next as well as which task. Finishing the task records which
boxes were checked. For a recurring task, the checklist comes back unchecked
for the next time.

### The pieces

- **The checklist.** An ordered list of short steps, edited in the task
  editor. On a routine it lives on the routine and every copy shares it, like
  notes. A copy can add steps of its own, which come after the shared ones.
- **Steps for this time only.** A step added from the task's detail (where
  you are when you find out about it) applies to this time only and goes away
  when the task is done or skipped. A step that should stay is added in the
  editor. A one-time task has only one time, so for it this choice doesn't
  matter.
- **Checking.** Check and uncheck in the detail, by click or by key. Checks
  belong to the current time and sync like everything else.
- **The row.** A task with a checklist shows its first unchecked step and a
  count: "Buy a cabin filter · 0/2". A fully checked task shows "2/2".
- **Done with boxes left.** `Space` on a task with unchecked boxes opens the
  usual done dialog, which lists the unchecked steps and asks to confirm
  ("2 steps aren't checked"). Why goes in the usual note. Done with every box
  checked works as it does today.
- **Reset.** Done or skipped, a recurring task comes back with every box
  unchecked and its this-time-only steps gone.
- **History.** Each completion keeps a snapshot of its steps and which ones
  were checked, so later changes to the checklist don't rewrite the past.
  History shows checked steps as done and unchecked ones as skipped. A skip of
  the whole task shows every step as skipped, with the reason in its note as
  today.
- **Ranking is unchanged.** Checked boxes don't make a task less needful. The
  task is due or it isn't, and progress shows only on the row.

### Breadboard

```
Needful list                              Detail: Oil change · Tacoma
────────────                              ───────────────────────────
▸ Oil change              Tacoma          [x] 6 qt 5W-30
    Oil filter · 1/4                      [ ] Oil filter
  Mow the lawn            Home            [ ] Reset the reminder
  Replace the cabin filter  CR-V          [ ] Write down the mileage
    Buy a cabin filter · 0/2              + Add a step for this time

Space on "Oil change"
  Done: Oil change
    2 steps aren't checked:
      Reset the reminder
      Write down the mileage
  Note: [Reminder button is broken          ]
  [ Done anyway ]  [ Cancel ]

History
  Oil change · Tacoma · Nov 21
    ✓ 6 qt 5W-30  ✓ Oil filter  – Reset the reminder  – Write down the mileage
    Reminder button is broken
```

### On the phone

The phone page does everything the desktop does with checklists, because
it's where you are when you do the work.

- **The row** shows the next unchecked step and the count under the title,
  the same as on the desktop: "Buy a cabin filter · 0/2".
- **The task sheet** lists the steps above its Done / Today / Snooze / Skip
  buttons, each a full-width row with a big tap target. One tap checks, and
  another tap unchecks. There's no confirmation and no save button.
- **Add a step** is a field at the end of the list, "Add a step for this
  time", with `enterkeyhint="done"`. Submitting it adds the step and keeps the
  keyboard up for the next one, so typing three things in the store aisle
  takes three Enters.
- **Done with boxes left** is the same as on the desktop: the done sheet
  lists the unchecked steps above the note, and its button reads "Done
  anyway".
- **Adding a task** from the `+` sheet gets an optional steps field, one step
  per line, so a "Hardware store" one-time task with its list takes a single
  form.
- **History** on the phone shows each completion's steps as done or skipped,
  the same as the desktop, both in the task sheet's history and in the History
  tab.

```
Phone · Needful                      Task sheet
───────────────                      ──────────
▌Replace the cabin filter   Due      CR-V
  Buy a cabin filter · 0/2           Replace the cabin filter
  CR-V                               [Due today] [Once]
▌Oil change                 2d late
  Oil filter · 1/4                   ┌────────────────────────────┐
  Tacoma                             │ ○  Buy a cabin filter      │
                                     │ ○  Swap it in              │
                                     │ +  Add a step for this time│
                                     └────────────────────────────┘
                              [+]    [ Done ] [Today] [Snooze] [Skip]
```

### Command line

```sh
needful add "Oil change" --context Tacoma --every 6m \
  --step "6 qt 5W-30" --step "Oil filter" --step "Reset the reminder"
needful step 3 "Buy a cabin filter"        # this time only
needful step 3 "Buy a cabin filter" --keep # part of the checklist
needful check 3 1                          # check step 1; --undo unchecks
needful done 3 --note "…"                  # unchecked steps need --yes
```

`--json` output gains `steps`, each with its text, whether it's checked, and
whether it's for this time only. Completions gain their snapshot.

## Rabbit Holes

- **Step identity.** Checks and edits have to survive reordering, renaming,
  and sync, so steps need ids, not positions. Use the store's usual ids. The
  CLI's `check 3 1` takes a position and turns it into an id.
- **Routine edits during a time.** A step added to the routine while a copy
  is half done shows up unchecked there. A step removed from the routine takes
  its check with it. Don't try to keep the current time's checks stable
  against routine edits.
- **Sync conflicts.** Checking the same step on two machines is the same
  result either way. Checking on one machine while the other finishes the
  task should leave the completion's snapshot as recorded, and the check
  falls to the reset. That way the snapshot is never rewritten.
- **Undo.** Undoing a done brings back the time's checks and this-time-only
  steps from the snapshot, so the snapshot has to keep them all, not only the
  unchecked ones.

- **Checking on a flaky connection.** The phone may be on one bar of
  cellular through the relay. A tap checks the box right away and sends it in
  the background. If the send fails, the box goes back and a toast says so.
  Sends go out in tap order, so quick taps can't arrive out of order.
- **Polling over a tap.** The page refreshes every few seconds. A refresh
  that lands while a check is still on its way mustn't flip the box back.
  Hold off rendering a step until the desktop has replied to its pending
  check.
- **Phone API.** Checking, adding a step, and steps in task and history
  payloads are new endpoints and fields in `webapi.cpp`. They should be the
  same store calls the desktop and the CLI use, not a separate path.

## No Gos

- Offline checking queued until the desktop can be reached again. Checks
  need the desktop to answer, like every other action on the phone page.

- Nested steps, or steps with their own dates, priorities, or contexts.
- Turning a step into a task, or a task into a step.
- Checked boxes changing needfulness or the due date.
- Blocking one task on another. The blocked-tasks pitch covers waiting on
  something you can't do yet. It's shelved until use shows that snooze and a
  checklist leave a gap.
