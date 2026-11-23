# Building an Omarchy app for macOS

How Needful goes from a Linux-first Qt/QML app to a double-clickable
`Needful.app` and a versioned dmg. The same recipe fits any Omarchy-style app
that is a Qt 6 + QML program with a command line mode and an SVG icon. The
working version is `bin/build-macos`; this explains why each step is there.

## What you need

- Xcode command line tools.
- Qt 6 for macOS, from the Qt online installer. It lands in
  `~/Qt/<version>/macos/`, which Homebrew does not put on `PATH`. The script
  looks there when `qmake6` is not on `PATH`.
- `rsvg-convert` (`brew install librsvg`) to turn the app's SVG into icons.
- Rust's `cargo` (from rustup), for the peer sync link in `link/`. `make`
  runs it, and links its static library with the Security,
  CoreFoundation and SystemConfiguration frameworks.
- Nothing else. `qmake`, `macdeployqt`, `iconutil`, `PlistBuddy`, `codesign`
  and `hdiutil` all ship with Qt or macOS.

## The steps

1. **Build with qmake.** On macOS `qmake6 app.pro && make` produces a
   `needful.app` bundle directly, with no extra `.pro` settings. Use
   `sysctl -n hw.ncpu`, not `nproc`, for the job count.
2. **Stage a copy as `Needful.app`.** See "Case-insensitive volumes" below.
3. **Make the icon.** Render the SVG at 16, 32, 128, 256 and 512 px, plus an
   `@2x` of each, into `Needful.iconset/` (`icon_16x16.png`,
   `icon_16x16@2x.png`, and so on), then
   `iconutil -c icns Needful.iconset -o Needful.app/Contents/Resources/Needful.icns`.
   The bundle has no `Resources` folder until `macdeployqt` runs, so create it
   first or `iconutil` fails with a bare "Failed to generate ICNS".
4. **Set the names in `Info.plist`.** With `PlistBuddy`, set `CFBundleName`,
   `CFBundleDisplayName` and `CFBundleIconFile` (no extension). That is what
   Finder, the Dock and the menu bar show.
5. **Run `macdeployqt`** with `-qmldir=<src>` so it finds the QML imports and
   copies the Qt frameworks and plugins into the bundle. It prints errors about
   SQL drivers (`libmimerapi`, `libiodbc`, Postgres). They are optional Qt
   plugins the app does not use; the bundle is still produced.
6. **Add the offscreen platform plugin** (see "Command line mode").
7. **Sign** every library and framework inside out, then the app, with the
   certificate in `NEEDFUL_CODESIGN_IDENTITY`, or ad hoc (`-s -`) without
   one. Editing the bundle after `macdeployqt` breaks its seal, and an
   unsigned or broken arm64 app can refuse to launch. Neither is Apple's
   distribution signing; see "Updates" for why the certificate matters.
8. **Pack the dmg.** Put the app and a symlink to `/Applications` in a folder,
   then `hdiutil create -volname Needful -srcfolder <folder> -format UDZO
   Needful-<version>.dmg`. Read the version from `VERSION` in the `.pro` so
   the file name always says which build it is.

## Things that bite

### Command line mode needs the offscreen platform

An Omarchy app that is also a CLI usually sets `QT_QPA_PLATFORM=offscreen` for
commands so they need no display. `macdeployqt` bundles only `cocoa`, so every
command aborts with "Could not find the Qt platform plugin offscreen". Copy
`$(qmake6 -query QT_INSTALL_PLUGINS)/platforms/libqoffscreen.dylib` into
`Contents/PlugIns/platforms/`. `macdeployqt` has no flag for this.

### A double-click is a launch with no arguments

On Linux the launcher runs `app open`, and bare `app` prints help. Finder runs
the executable with no arguments, so the app took the "print help" path, forced
offscreen, and (before the plugin was bundled) crashed on launch. Detect the
bundle in `main()`: no arguments and `argv[0]` containing
`.app/Contents/MacOS/` means open the window. Bare `app` from a terminal still
prints help.

### The volume is case-insensitive

`Needful.app` and `needful.app` are the same path. qmake builds `needful.app`,
so an `rm -rf Needful.app` before copying deletes the build output and `make`
then reports "Nothing to be done". Copy the built bundle into a separate
folder (`build/macos/`) and do all the edits there.

### Keep the CLI name lowercase

Only the bundle and display name are capitalized. The executable inside stays
`needful`, as does `setApplicationName`, so the data folder
(`~/Library/Application Support/needful`) and the command do not change and
existing data keeps working.

## Background services

Anything an Omarchy app installs as a systemd user unit (a server, a sync
timer, a helper daemon) becomes a launchd agent on macOS. Needful's
`serve install`, `sync install` and `sync rsync install` are the worked
examples.

- **Where:** a plist at `~/Library/LaunchAgents/<label>.plist`. Name the label
  after the bundle id plus the job (`kjlape.needful.serve`), and
  the file after the label.
- **Long-running job:** `RunAtLoad` and `KeepAlive` true, with
  `ThrottleInterval` (5) to match systemd's `RestartSec`. That replaces
  `Restart=on-failure` and `WantedBy=default.target`.
- **Timer:** there is no timer unit. Use one job with `StartInterval` in
  seconds and `RunAtLoad`, and no `KeepAlive`. Convert `5m` or `1h` yourself.
  Two commands that systemd runs as two `ExecStart=` lines become one
  `/bin/sh -c 'first && second'`, with every word shell-quoted.
- **No enable step:** load the file yourself, from the install command, with
  `launchctl bootout gui/$UID/<label>` (ignore failure) then
  `launchctl bootstrap gui/$UID <file>`. The bootout makes reinstalling work.
  A loaded agent starts now and at every login. Uninstall is `bootout` and
  deleting the file. `launchctl kickstart gui/$UID/<label>` starts it by hand.
- **XML:** escape `&`, `<` and `>` in every argument. A `&&` in a shell line
  must be written `&amp;&amp;`.
- **Logs:** launchd has no journal. Point `StandardOutPath` and
  `StandardErrorPath` at `~/Library/Logs/<app>-<job>.log`, and create that
  folder if needed.
- **The binary path is baked in.** The plist holds the executable's absolute
  path, so install from `/Applications/Needful.app/Contents/MacOS/needful`,
  not from a build folder, and rerun the install if the app moves.
- **No `%t` runtime folder.** Unix socket paths have no per-user runtime
  directory, so keep them under the app's data folder, and mind the 104
  character limit on a socket path.
- **launchd's `PATH` is minimal** (`/usr/bin:/bin:/usr/sbin:/sbin`), without
  Homebrew. Give absolute paths to anything you call, or the job will not
  find it. ssh works with a key that needs no prompt; launchd passes the
  login session's ssh agent.
- **Tests must never load real agents.** An install test that runs on macOS
  will otherwise load agents into the developer's own session. Point `HOME`
  at a temporary folder and take `launchctl` off `PATH`, and have the install
  skip the load when `launchctl` is missing.

### rsync on macOS

`/usr/bin/rsync` is `openrsync` (rsync 2.6.9 compatible). It lacks
`--protect-args` and `--mkpath`, so a command written for rsync 3 fails with
"unrecognized option". Prefer Homebrew's rsync (`/opt/homebrew/bin/rsync`)
when it is there, detect `openrsync` or a major version below 3 with
`rsync --version`, and for the old one drop those flags and single-quote the
remote path, since the other computer's shell now reads it.

## Updates

Releases go out like Flip's Flatpak repository: signed, from GitHub Pages,
and checked against a key the app was installed with. On macOS that's
[Sparkle](https://sparkle-project.org), which `bin/build-macos` downloads
(a pinned version, checked against its hash) and builds in when
`macos/sparkle-public-key` exists.

| Flatpak | Sparkle |
| --- | --- |
| GPG key signs the OSTree repo | An Ed25519 key signs each dmg and the feed |
| The `.flatpakref` pins the public key | `SUPublicEDKey` in the installed app's `Info.plist` pins it |
| The repo on `gh-pages` | `appcast.xml` and the newest dmgs on `gh-pages` |
| `flatpak update` | The app checks daily and offers the update; Check for Updates… in the app menu checks now |

The first download is trusted because it came over HTTPS from your Pages
site; after that, the app installs only what the same key signed, so a
compromised GitHub account can't ship an update. `SURequireSignedFeed` makes
the feed itself signed too, and `SUVerifyUpdateBeforeExtraction` checks a dmg
before anything in it is unpacked.

### Set up once, on the Mac that releases

1. **The signing key.** Run `bin/build-macos` once so Sparkle is downloaded,
   then `build/sparkle/<version>/bin/generate_keys`. It keeps the private key
   in the login Keychain and prints the public key. Put the public key in
   `macos/sparkle-public-key` and commit it. Back up the private key with
   `generate_keys -x needful-sparkle.key` and keep it offline: without it,
   nobody gets another update until they reinstall by hand.
2. **A code signing certificate.** Keychain Access, then Certificate
   Assistant, then Create a Certificate: a name like "Needful Self-Signed",
   identity type Self Signed Root, certificate type Code Signing. Then
   `export NEEDFUL_CODESIGN_IDENTITY="Needful Self-Signed"`. An ad hoc
   signature is different on every build, so macOS treats each update as a
   new app and asks again for Keychain items and permissions; one
   certificate keeps the app's identity, and Sparkle checks each update was
   signed with it. Back it up too (export it as a .p12).
3. **GitHub Pages.** The repository is private, so Pages needs a paid GitHub
   plan (the site itself is public). The dmgs live on `gh-pages`, not in
   GitHub releases, because a private repository's release downloads need a
   login. GitHub refuses files over 100 MB.

### Each release

Bump `VERSION` (and `pkgbuild/PKGBUILD`), commit, then on the Mac:

```sh
bin/release-macos            # tags v<version>, builds, prepares build/pages
bin/release-macos --publish  # pushes the tag and gh-pages
```

The release notes are the commit subjects since the last tag. The newest
three versions stay downloadable. Copies that run the phone page's service
(`serve install`) should run `needful serve restart` after updating, as on
Linux.

## What this does not do

- **Distribution signing and notarization.** A self-signed certificate does
  nothing for Gatekeeper, so on another Mac the first launch is refused until
  the user clicks Open Anyway in System Settings, Privacy & Security, or runs
  `xattr -dr com.apple.quarantine /Applications/Needful.app`. Updates after
  that install without the prompt. Removing it needs a Developer ID
  certificate (paid), the hardened runtime, and `notarytool`; the same
  pipeline takes them by changing the identity and adding a notarize step.
- **Universal binaries.** It builds for the architecture of the Mac it runs on
  (arm64 on Apple silicon).
- **A minimum macOS other than Qt's default** (13 for Qt 6.11).

## Checklist for a new app

- [ ] `.pro` has `VERSION`; the script reads it for the dmg name.
- [ ] An SVG icon, square, at a path the script can read.
- [ ] `main()` opens the window when launched from inside a `.app` with no
      arguments.
- [ ] Commands that use `offscreen` have the plugin copied into the bundle.
- [ ] `bin/build-macos` runs clean, then `Needful.app/Contents/MacOS/<name>
      --help` prints help and a double-click opens the window.
