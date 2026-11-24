---
name: needful
description: Read and update the person's Needful list (routine chores and recurring tasks ranked by how needful they are, grouped by context such as Home, a vehicle, work, or a channel) with the needful command. Use when asked what needs doing, to add or finish a chore, to log what was done on a car or project, or when the last time something was done matters.
---

# Needful

`needful` keeps routine tasks ranked by how needful they are: priority, grown
by how late each task is relative to its own rhythm. Every command below needs
no display, takes `--json`, exits 0 or 1, and writes errors to stderr.

- `needful list [--view needful|today|upcoming|all] [--context "Automotive/White Honda CR-V"] --json`
- `needful add "Change the air filter" --context Home --every 1m [--priority high] [--months 5-9] [--fixed] [--ask] [--due 2026-05-01]`
- Repeat `--context` to add a routine shared by several contexts (one oil change for every vehicle); each context gets its own copy with its own id, due date, and history.
- `needful done <id> --note "5W-30, 84,210 miles"` (tasks made with `--ask` also take `--next YYYY-MM-DD`; without it they retire). A task with unchecked steps refuses without `--yes`; say why in `--note`.
- Checklists: `needful add ... --step "Oil filter" --step "Reset the reminder"` gives a task its steps. `needful step <id> "Buy a filter"` adds one for this time only (`--keep` makes it part of the checklist; on a routine, every copy's). `needful step <id>` lists them, numbered; `needful check <id> <n> [--undo]` checks one. Task JSON has `steps` (`text`, `checked`, `once`, `shared`), `nextStep`, and `stepsText`; completions have `steps` as they were. Checking steps doesn't change how needful a task is.
- `needful skip <id> [--reason "Rained out"]` passes over the time it's next due without doing it: history records the skip (`"skipped": true`, the reason as its note) and the task moves to its next time. It takes `--next` like `done`.
- `needful snooze <id> <days>`, `needful today <id>` (pull into today, or put back)
- `needful log "Squeak from the front left at low speed" --context CR-V [--follow-up] [--priority high] [--due 2026-05-01]` notes something on a context; `--follow-up` also adds a one-time task to deal with it.
- Notes on one time of a task: `needful log "Drain plug was stripped" --task <id>` notes the coming time (it goes into history with it when done or skipped); `--task <id> --last` notes the last time it was done or skipped; `--done <n>` a particular past time, by the number `needful history --task <id>` shows as `#n`. These take the context from the task, so leave out `--context`; `--follow-up` still works. Keep using `done --note` for what's said on finishing.
- `needful history [--context C] [--search oil] --json` answers "when did I last…"; it interleaves completions (`"kind": "completion"`, with `notes` about that time) with notes (`"kind": "log"`; a note about a time has `about` (task id), `aboutTitle`, `aboutDone` (its completion, or 0), and `thisTime`). `needful history --task <id> [--json]` lists one task's times, newest first, each with its notes. Task JSON has `notesThisTime`.
- `needful contexts [add "Parent/Child"] --json`
- Every command takes `--universe <name>` for another list on this computer, such as a partner's or the house's (`needful universe --json` lists them). Without it, commands use the person's own list; only use another universe when asked about it.
- `needful sync status --json` says whether this machine syncs with the person's other computers, and when each was last heard from. Commands work the same either way. `needful universe members <name> --json` says who a universe is shared with. Leave `universe share`, `universe join`, `universe unshare`, `sync pair`, `sync join`, `sync unpair`, `sync reach`, `sync relay`, `sync install`, `sync setup`, `sync rename`, `sync forget`, and `sync rsync` to the person.
- `needful serve status --json` says whether the phone page is being served, and its tailnet URL. Leave `serve install`, `serve login`, `serve restart`, and `serve uninstall` to the person: they change systemd and Tailscale.

Ask before marking things done, skipping them, or deleting on the person's behalf. History is
the person's record; never remove entries from it.
