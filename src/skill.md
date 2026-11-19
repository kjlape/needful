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
- `needful done <id> --note "5W-30, 84,210 miles"` (tasks made with `--ask` also take `--next YYYY-MM-DD`; without it they retire)
- `needful snooze <id> <days>`, `needful today <id>` (pull into today, or put back)
- `needful log "Squeak from the front left at low speed" --context CR-V [--follow-up] [--priority high] [--due 2026-05-01]` notes something on a context; `--follow-up` also adds a one-time task to deal with it.
- `needful history [--context C] [--task id] [--search oil] --json` answers "when did I last…"; it interleaves completions (`"kind": "completion"`) with notes (`"kind": "log"`).
- `needful contexts [add "Parent/Child"] --json`
- `needful sync status --json` says whether this machine syncs with the person's other computers, and when each was last heard from. Commands work the same either way. Leave `sync setup`, `sync rename`, `sync forget`, and `sync rsync` to the person.
- `needful serve status --json` says whether the phone page is being served, and its tailnet URL. Leave `serve install`, `serve login`, and `serve uninstall` to the person: they change systemd and Tailscale.

Ask before marking things done or deleting on the person's behalf. History is
the person's record; never remove entries from it.
