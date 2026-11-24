# Releasing for Linux

Linux gets two channels, both on the same GitHub Pages site and signed with
the same GPG key (`NEEDFUL_GPG_KEY`): a pacman repository for Arch, and a
Flatpak repository for every other distribution. Release both from the same
commit; each script keeps the other's files and section of the page.

```sh
bin/release-arch --publish
bin/release-flatpak --publish
```

## Arch

`bin/release-arch` publishes Needful as a pacman repository on GitHub Pages,
like Flip's Flatpak repository: static files on `gh-pages`, signed with a
GPG key each machine trusts once. Machines that add it get every release
with `pacman -Syu`, and pacman refuses a package or package list that key
didn't sign.

| Flatpak | pacman |
| --- | --- |
| GPG key signs the OSTree repo | The same kind of key signs each package and the database |
| The `.flatpakref` pins the key | `pacman-key --lsign-key` trusts it, on that machine only |
| The repo on `gh-pages` | `arch/x86_64/` on `gh-pages` |
| `flatpak update` | `pacman -Syu` |

### Set up once

1. **devtools**, for the clean chroot: `sudo pacman -S devtools`. The
   package is built from an export of the tagged commit in a fresh Arch
   chroot, so it can't pick up anything only this machine has, like a
   dependency the PKGBUILD forgot. Building the chroot asks for sudo.
2. **A signing key.** Use Flip's, so people trust one key for both, or make
   one for Needful: `gpg --quick-gen-key "Needful releases" ed25519 sign never`.
   One key for everything is fewer steps for users; one per app means a
   leaked key compromises only that app. Then
   `export NEEDFUL_GPG_KEY=<its fingerprint>`, and back up the secret key
   (`gpg --export-secret-keys --armor <fingerprint>`) somewhere offline:
   without it, every machine has to trust a new key by hand.
3. **GitHub Pages**: free for a public repository; a private one needs a
   paid plan. The site is public either way.

### Each release

Bump `VERSION` in `needful.pro` and `pkgver` in `pkgbuild/PKGBUILD`, commit,
then:

```sh
bin/release-arch            # tags v<version>, builds, signs, prepares build/pages
bin/release-arch --publish  # pushes the tag and gh-pages
```

`NEEDFUL_NO_CHROOT=1 bin/release-arch` rehearses with plain makepkg,
without devtools; such a build can't be published.

Only the newest package stays, since pacman installs only the newest. The
debug package isn't published. Releasing from the same commit on the Mac
(`bin/release-macos`) adds the macOS download to the same page; each script
keeps the other's files.

### How it's put together

- **The PKGBUILD builds either tree.** `bin/install` builds this checkout
  (`$startdir/..`). The chroot can't see it, so `bin/release-arch` exports
  the commit with `git archive` and fills in `_tarball` and `_sha256` in a
  copy of the PKGBUILD.
- **No symlinks.** `repo-add` makes `needful.db` a symlink to
  `needful.db.tar.gz`, and GitHub Pages doesn't serve symlinks, so each
  becomes a copy.
- **`repo-add --verify`** refuses to add to a published database whose
  signature doesn't check out, so a tampered `gh-pages` stops the release.
- **One parentless commit.** `gh-pages` holds only what's published now, so
  old packages don't pile up in its history.
- **x86_64 only.** The PKGBUILD also allows aarch64, but nothing builds it
  here.

### What people run

The page at the Pages site shows this with the real fingerprint:

```sh
curl -fsSLO https://kjlape.github.io/needful/arch/needful.gpg
sudo pacman-key --add needful.gpg
sudo pacman-key --lsign-key <fingerprint>
```

Then, in `/etc/pacman.conf`:

```ini
[needful]
SigLevel = Required
Server = https://kjlape.github.io/needful/arch/$arch
```

and `sudo pacman -Syu needful`. A copy installed with `bin/install` has the
same name, so the repository's next release replaces it.

## Flatpak

`bin/release-flatpak` publishes an OSTree repository, as Flip does: every
commit signed, static deltas so updates download only what changed, and the
last three releases kept. People install with one command:

```sh
flatpak install --user https://kjlape.github.io/needful/flatpak/io.github.kjlape.Needful.flatpakref
```

and update with `flatpak update`. The `.flatpakref` carries the public key,
so Flatpak trusts it from that first install on.

### Set up once

- `sudo pacman -S flatpak ostree appstream`. The first build downloads the
  KDE SDK and its Rust extension, a few gigabytes, into your user Flatpak
  installation.
- The same `NEEDFUL_GPG_KEY` as for Arch.

### Each release

```sh
bin/flatpak                  # build this checkout and install it, to try it
bin/release-flatpak          # tags v<version>, builds, signs, prepares build/pages
bin/release-flatpak --publish
```

`bin/flatpak` also writes `build/needful.flatpak`, a single file that
installs anywhere (`flatpak install --user needful.flatpak`) but never
updates.

### How it's put together

- **The manifest** (`flatpak/io.github.kjlape.Needful.yml`) builds the
  checkout on the KDE runtime, with the Rust SDK extension for the link.
  cargo fetches the crates `Cargo.lock` pins during the build
  (`--share=network` in the build only). Flathub would want them vendored.
- **The metainfo** in the repository has no releases. `bin/release-flatpak`
  builds the tagged commit in its own checkout, adds that release's entry
  (the commit subjects since the last tag) and points the screenshot at the
  tag, then validates it.
- **The repository starts from what's published.** `build/pages` is reset to
  `origin/gh-pages`, and the empty directories git drops are made again, so
  every earlier release stays in the OSTree history.
- **The sandbox** has the network (the link and the phone page), Wayland, and
  read-only access to the Omarchy theme and language. Data lives in
  `~/.var/app/io.github.kjlape.Needful/data/needful`, apart from a native
  install's.
- **What it can't do**: install services (`serve install`, `sync install`,
  `sync rsync install`) or the agent skill, since the sandbox reaches neither
  systemd nor your home folder. Those commands say so and show what to run
  instead. rsync and tailscale aren't in the sandbox either.

### Changing the key

Existing installs stop updating until people run
`flatpak remote-modify --user --gpg-import=<new key file> needful`. Say so in
the release notes, from the old key's last release.

### Flathub

Not from here. Flathub's policy forbids AI tools from writing its manifests
or submissions, so a Flathub listing is yours to do by hand, with any
AI-generated parts disclosed, and it wants real release history first,
which this repository builds.
