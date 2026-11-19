# Needful

![Needful showing the needful list](docs/screenshot.png)

A to-do list that can't pile up. Needful holds the routine things that keep a
life running (mowing, air filters, oil changes, invoices, the next video) and,
whenever you have a moment, shows you a short list ranked by how needful each
one is. Pick from the top, and trust that nothing is forgotten.

- **Needfulness** is priority, grown by how late a task is in units of its own
  rhythm. A weekly chore a week late counts as much as a monthly one a month
  late, so a high-priority mow wins on Saturday while the neglected air filter
  climbs steadily until it wins too.
- **Contexts** group tasks by where you do them: Home, Automotive › White
  Honda CR-V, Work › a client, YouTube. Contexts nest, and picking one filters
  every list to it and everything inside it.
- **Routines** share one task across several contexts: tick all three vehicles
  on "Oil change" and each gets its own copy, with its own due date, snooze,
  and history. Edits to the routine reach every copy, and a copy can keep its
  own schedule, priority, season, and notes ("Only Tacoma" in the editor).
- **Today** is the day's inbox. Pull tasks into it with `T`; it starts clean
  every morning, and what's done shows below, crossed off.
- **Snooze** hides a task for a while without pretending it's done.
- **Schedules** repeat from when you did it, on a fixed rhythm however late,
  ask you when to come back each time you finish, or happen once. A task can
  be in season only some months; the time out of season doesn't count as late.
- **History** keeps every completion with its note, under its context, so at
  the car you can see when the oil was last changed and with what.

## Keys

| Key | Does |
| --- | --- |
| `Space` | Done, with a note |
| `T` | Pull into today, or put back |
| `S` | Snooze |
| `N` | New task |
| `E` / `Enter` | Edit |
| `X` / `Delete` | Delete (history stays) |
| `J` / `K`, `↓` / `↑` | Next / previous |
| `1` – `4` | Today, Needful, Upcoming, History |
| `[` / `]` | Previous / next context |
| `0` | Every context |
| `C` | New context inside the current one |
| `Shift+C` | New top-level context |
| `/` | Search |
| `Ctrl+Z` / `Ctrl+Shift+Z` | Undo / redo |
| `?` | Show the shortcuts |
| `Q` / `Ctrl+Q` | Quit |

## Command line

Every command needs no display, takes `--json`, and exits 0 or 1:

```sh
needful list [--view needful|today|upcoming|all] [--context "Automotive/White Honda CR-V"]
needful add "Change the air filter" --context Home --every 1m
needful add "Mow the lawn" --context Home --every 1w --priority high --months 4-10
needful add "Oil change" --context CR-V --context Tacoma --context Civic --every 6m
needful done 3 --note "Mobil 1 5W-20, 84,210 mi"
needful history --context "White Honda CR-V" --search oil
needful contexts add "Automotive/White Honda CR-V"
needful skill install   # teaches coding agents to use it
```

## Data

Everything lives in one JSON file in the platform's app data folder
(`~/.local/share/needful/needful.json` on Linux, inside the sandbox under
Flatpak, `~/Library/Application Support/needful` on macOS). `NEEDFUL_DATA` or
`--data` points it elsewhere. Every change saves at once, atomically, with the
previous version kept beside it as `needful.json.bak`. The window notices when
the command line or a sync tool changes the file, and every change is applied
on top of whatever another process saved, never over it.

## On your phone

`needful serve` runs a small web version for the phone: Today, Needful,
Upcoming, and History by context, marking things done with a note, pulling
into today, snoozing, logging a note, and quick tasks. It installs to the home
screen as an app. It listens on 127.0.0.1:8787 only, and has no password, so
Tailscale is how it reaches the phone.

Keep it running as a systemd user service, with a Tailscale node of its own
named `needful` (a userspace `tailscaled`, separate from the system's, so this
machine's identity on the tailnet doesn't change):

```sh
needful serve install --tailscale
systemctl --user daemon-reload
systemctl --user enable --now needful-serve.service needful-tailscale.service
needful serve login     # once: open the link it prints to add the node
needful serve status    # http://127.0.0.1:8787/ and https://needful.<tailnet>.ts.net/
```

The tailnet needs MagicDNS and HTTPS certificates turned on (Tailscale admin
console, DNS). Without `--tailscale`, only the first service is installed, and
the page stays on this machine. `needful serve uninstall` stops and removes
the services.

## Build

```sh
bin/build     # build/needful
bin/test      # the tests, offscreen
bin/install   # an Arch package, so it shows up in the app launcher
```

Needful is C++ and Qt Quick with no platform-specific dependencies: no D-Bus,
no portals, and the Omarchy theme and language are followed when present and
fall back cleanly when not, which keeps Flatpak and macOS builds open.

## Next

- Meter triggers: log a vehicle's odometer, and the oil change falls due at a
  mileage.
- Translations: every string goes through `qsTr`/`tr`; catalogs go in
  `src/i18n/`.

## License

MIT. See [LICENSE](LICENSE).
