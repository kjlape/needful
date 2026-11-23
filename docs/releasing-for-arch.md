# Releasing for Arch

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

## Set up once

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

## Each release

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

## How it's put together

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

## What people run

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
