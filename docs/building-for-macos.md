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
7. **Sign ad hoc** with `codesign --force --deep -s - Needful.app`. Editing the
   bundle after `macdeployqt` breaks its seal, and an unsigned or broken
   arm64 app can refuse to launch. This is not distribution signing.
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

## What this does not do

- **Distribution signing and notarization.** The result is ad hoc signed, so
  other Macs show a Gatekeeper warning until the user right-clicks and picks
  Open. Real distribution needs a Developer ID certificate, `codesign` with the
  hardened runtime, and `notarytool`.
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
