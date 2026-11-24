---
status: pitched
pitched: 2026-11-22
started: ~
completed: ~
audited: ~
slices: ~
---

# Phone page editing: change your list from the phone, not just work through it

## Problem

The phone page is good for working through the list: mark done with a note,
skip, snooze, pull into today, check off steps, log a note, add a quick task.
It can't change anything about a task.

That shows up quickly away from the desk. The mechanic says the next oil
change is at six months, not three, and there's no way to change the
rhythm. A one-off reminder added from the phone turns out to be a monthly
chore, and it can't be made to repeat. The gutter task belongs in season
only from October to December. A task you'll never do again needs to go.
Each of these becomes "fix it when I'm back at the computer," which is the
remembering Needful is meant to take off you, and by the time you're back,
it's forgotten.

New tasks from the phone are also thinner than tasks from the desktop: a
title, one context, a priority, and a due date. Anything with a schedule,
a season, or more than one context has to be made at the desk.

As more people reach the page without a desk nearby (phone link, and anyone
whose always-on Needful is a box in a closet), the page is increasingly the
only window they have open.

## Appetite

One cycle. Scope covers editing and deleting a task from the phone page,
the full set of task fields when adding one, undo for the page's own
changes, and adding a context. No change to how the page is reached or
paired, and no new task behaviour the desktop doesn't already have.

## Solution

### What changes for the user

- **Edit from a task's detail.** The task sheet gains an Edit button that
  opens the same fields the desktop editor has: title, contexts, priority,
  schedule (from when you did it, fixed rhythm, ask each time, once),
  rhythm, next due, season, and notes.
- **Routines work the way they do on the desktop.** Ticking more than one
  context makes a routine with a copy in each. Editing a copy offers the
  same choice the desktop does: change the routine, or keep this copy's own
  schedule, priority, season, and notes ("Only Tacoma").
- **Adding a task offers the same fields,** with the quick ones up front
  (title, context, due) and the rest under "More": schedule, rhythm,
  season, priority, notes, steps. A task added with just a title still
  works as it does today.
- **Delete, with a way back.** Delete sits at the bottom of the editor and
  asks once. History stays, as on the desktop.
- **Undo after every change.** The toast after any change the page makes
  (done, skip, snooze, today, edit, delete, log) carries an Undo button for
  a few seconds. It undoes that one change, the same as `Ctrl+Z` would
  have.
- **Add a context while adding a task.** The context picker ends in "New
  context…", which asks for a name and where it goes, so "Automotive ›
  Blue Tacoma" can be made on the spot.

### Editing breadboard

```
Task sheet                        Editor sheet                    Back on the list
──────────                        ────────────                    ────────────────
Oil change                        Title      [Oil change      ]
Automotive › White CR-V           Contexts   [x] White CR-V
due in 2 weeks · every 3 months              [x] Blue Tacoma
                                             [ ] New context…
[Done] [Today] [Snooze] [Skip]    Schedule   [From when done ▾]
[Edit] ───────────────────────→   Every      [6] [months ▾]
                                  Next due   [2027-02-14]
                                  Season     [All year ▾]
                                  Priority   [●●○]
                                  Notes      [5W-30, 4.5 qt   ]
                                  This copy  ( ) Whole routine
                                             (•) Only White CR-V
                                  [Save] [Cancel]
                                  [Delete task]  ───────────────→ "Deleted Oil change" [Undo]
                                  [Save] ───────────────────────→ "Saved" [Undo]
```

### Old and new

A page newer than the Needful serving it hides what that Needful can't do,
as it already does for logging. Against an older Needful, the page shows no
Edit button, the add sheet's "More" stays closed, and toasts carry no Undo.

## Rabbit Holes

- **Sharing undo with the window.** It's tempting to make the phone's Undo
  and the window's `Ctrl+Z` one history. They're separate people's hands
  on separate screens, and a shared stack means the window undoing
  something done on the phone a minute ago. The page undoes only the
  change it just made, and only from its own toast.
- **Rebuilding the desktop editor in the browser field for field.** The
  desktop editor is laid out for a wide window and a keyboard. Take its
  fields and its rules, not its layout: one column, native date and select
  inputs, sections that fold.
- **Full context management.** Renaming, moving, and deleting contexts
  touches every task under them and deserves the desktop's view of the
  whole tree. Adding one from the picker covers what comes up on a phone.
- **Editing history.** Fixing a past entry's date or note is a fair want,
  but it's a separate set of rules about what a changed past means for
  what's due next. Leave it out.
- **Two people editing at once.** The window and the page may both have
  the same task open. Last save wins, as it already does between the
  window and the command line. Don't build locking or merge prompts.

## No Gos

- No change to how the page is reached: still `serve`, Tailscale, or phone
  link once it lands.
- No universe switching on the page. Each universe keeps its own page.
- No pairing, sync settings, or device list on the page.
- No renaming, moving, or deleting contexts.
- No editing past entries in history.
- No keyboard shortcuts on the page.
- No offline changes. If the Needful serving the page can't be reached, the
  page says so and waits, as today.
- No new task fields or behaviour beyond what the desktop editor has.
