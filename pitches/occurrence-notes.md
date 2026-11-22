---
status: pitched
pitched: 2026-11-21
started: ~
completed: ~
audited: ~
slices: ~
---

# Notes on a time: log entries tied to one occurrence of a task

## Problem

What you notice about a task usually belongs to one particular time of it.
The drain plug on the Tacoma was stripped at this oil change. The lawn was
too wet to bag at Saturday's mow. Two weeks after the furnace filter went in
it was already grey, which says something about that filter, not the next
one.

Today there are two places to put it, and neither fits:

- **The done note.** It's one note, written when you finish. Anything you
  notice while you're still working, like "the 13 mm socket is in the
  garage, not the truck", has nowhere to go until you're done, so it's lost
  or retyped from memory. Anything you learn afterwards ("the filter failed
  two weeks later") can't be added at all.
- **The context Log (`L`).** It's tied to the CR-V or the house, not the oil
  change, so it doesn't sit next to the time it's about. Reading a task's
  history, you see "Oil change · Nov 21" in one place and "drain plug
  stripped" somewhere else, with nothing saying they belong together.

So the question you actually ask at the car, "what happened last time I did
this?", gets half an answer.

## Appetite

One small cycle. Scope covers notes on the coming time of a task and on any
past time (done or skipped), shown under that time in the task's history
and rolled up into the context Log, in the app, the command line, the phone
page, and sync. Notes stay plain text, like Log entries today.

## Solution

### The idea

A note can be about one time of a task, either the time that's coming up or
one that's already been done or skipped. It's still a full Log entry. It
lives in the task's context, can raise a follow-up, and shows up in the main
Log and in History with everything else. It also knows which time it's
about, so the task's own history shows it under that time.

Notes written on the coming time go with it when the task is done or
skipped. You can keep notes while you work and they land on the completion
without being retyped.

### The pieces

- **A note on this time.** `Shift+L` on a selected task opens the Log dialog
  already about that task's coming time ("About: Oil change · Tacoma · this
  time"). The context is the task's and isn't asked for. Follow-up works as
  it does today. `L` doesn't change: a note on the context alone.
- **A note on a past time.** In History, with a completion or skip selected,
  `Shift+L` opens the same dialog about that time ("About: Oil change ·
  Tacoma · Nov 21"). In the task's detail, each history entry has an "Add a
  note" affordance that does the same.
- **The task detail** shows "Notes this time" above the history: what's been
  noted on the coming time, newest last, each removable.
- **Done and skip.** The done dialog lists this time's notes above its own
  note field, so you don't write them twice. The done note stays what it is
  now, the note you write when you finish. When the task is done or skipped,
  this time's notes move with it into history, and "Notes this time" starts
  empty for the next time.
- **The task's history.** Each time shows its done note (or skip reason),
  its checklist snapshot, and then the notes about it, oldest first, each
  with its date.
- **The main Log and context History.** A note about a time shows in its
  context's History with the other notes, and says which task it's about:
  "Oil change · Drain plug was stripped". Picking it selects the task.
  Context-only notes look the same as today.
- **Routines.** Each copy has its own times, so a note on the Tacoma's oil
  change is about the Tacoma's, not the CR-V's.
- **Undo.** Undoing a done puts that time back as the coming time, and its
  notes come back to "Notes this time".
- **Deleting.** Removing a task keeps its history, and its notes stay with
  it under the title they were written about. Removing a completion from
  history keeps its notes in the Log as context notes that still name the
  task. Removing a note removes only the note.

### Breadboard

```
Needful list                          Detail: Oil change · Tacoma
────────────                          ───────────────────────────
▸ Oil change          Tacoma          [x] 6 qt 5W-30
    Oil filter · 1/4                  [ ] Oil filter
                                      Notes this time
Shift+L                                 Nov 21  Socket's in the garage, not the truck
  Log a note                            + Add a note
  About: Oil change · Tacoma · this time
  [Drain plug was stripped, used a     ]      History
  [ oversize plug                      ]        Nov 21  …
  [ ] Follow up                                 May 14  Mobil 1 5W-30, 84,210 mi
  [ Log ]  [ Cancel ]                                   + Add a note

Space on "Oil change"
  Done: Oil change
    Noted this time:
      Socket's in the garage, not the truck
      Drain plug was stripped, used an oversize plug
  Note: [Mobil 1 5W-30, 91,040 mi          ]
  [ Done ]  [ Cancel ]

Task history                            Context History (Tacoma)
  Oil change · Nov 21                     Nov 21  ✓ Oil change
    ✓ 6 qt 5W-30  ✓ Oil filter                    Mobil 1 5W-30, 91,040 mi
    Mobil 1 5W-30, 91,040 mi              Nov 21  ✎ Oil change · Drain plug was stripped
    ✎ Nov 21  Socket's in the garage      Nov 21  ✎ Oil change · Socket's in the garage
    ✎ Nov 21  Drain plug was stripped     Nov 18  ✎ Squeak from the front left
    ✎ Dec 3   Slight seep at the plug             (no task: a context note)
```

### On the phone

The phone page is where you are when you notice things, so it can write
these notes too.

- **The task sheet** has a "Note this time" field under the checklist. A
  submitted note shows in the list above it right away, and the keyboard
  stays up for another.
- **The done sheet** lists this time's notes above its note field, as on the
  desktop.
- **History** in the task sheet and in the History tab shows notes under
  the time they're about. Tapping a past time offers "Add a note".

### Command line

```sh
needful log "Drain plug was stripped" --task 3           # the coming time
needful log "Slight seep at the plug" --task 3 --last    # the last time it was done or skipped
needful log "Filter already grey" --done 41              # a particular past time
needful log "Squeak from the front left" --context CR-V  # as today: the context only
```

`--task` and `--done` take the context from the task, so `--context` isn't
needed with them. `--follow-up` works with all of them.

```
$ needful history --task 3
2026-11-21  Oil change  [Automotive › Tacoma]  #41
      1. [x] 6 qt 5W-30   2. [x] Oil filter
      Mobil 1 5W-30, 91,040 mi
      note 2026-11-21: Socket's in the garage, not the truck
      note 2026-11-21: Drain plug was stripped
2026-05-14  Oil change  [Automotive › Tacoma]  #29
      Mobil 1 5W-30, 84,210 mi
```

The task's history shows each time's id, so `--done` has something to take.
Context history shows notes about a time as `note: Oil change · …`. In
`--json`, a note carries the task and the time it's about, a completion
carries its notes, and `list --json` gives each task its notes for this
time.

## Rabbit Holes

- **Matching notes to times by date.** It's tempting to put a note under
  whichever completion is nearest in time. That breaks as soon as a task is
  done twice in a day, done late, or a note is added weeks afterwards. Notes
  have to name the time they're about, the same way this-time-only steps
  and checks already do.
- **History from before this feature.** Completions recorded before now
  don't say which time they were, so a note on an old time needs that worked
  out from the order of the task's history. That's right unless completions
  have been removed. Do it the simple way and accept that, rather than
  building a repair tool. Old context notes stay context notes. Don't try
  to guess which task each one was about.
- **Sync with a done on another computer.** You note the coming time on the
  laptop while the desktop marks the task done. The note should land on the
  completion the desktop made, not on the next time. That's the reason
  notes name the time they were written on rather than "whatever is
  current". Don't move a note after the fact to "fix" this.
- **Folding the done note into the notes.** Making the done note just
  another note, or turning notes into the done note, rewrites every
  existing completion and changes what `done --note` means. Keep them
  separate. The done note is what you say when you finish, and the notes
  are everything else about that time.
- **Phone polling over a fresh note.** A refresh that lands while a note is
  still on its way mustn't drop it from the list. Hold it in place until the
  desktop has answered, as with checks.

## No Gos

- Editing a note after it's written. Remove it and write it again, as with
  Log entries today.
- Notes on a routine as a whole, or on every copy at once. A note is about
  one time of one copy.
- Notes on a future time beyond the coming one ("next time, buy two
  filters"). That's a this-time-only step on the coming time, or a note on
  the task itself.
- Photos, attachments, mileage fields, or any structured data on a note.
- Moving an old context note onto a task's time.
- Notes changing needfulness, the due date, or the schedule.
