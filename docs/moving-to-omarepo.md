# Moving Needful onto omarepo

Needful publishes today from `../needful/bin/release-*`: the pacman repository
is on the GitHub release marked Latest, the Flatpak repository and the appcast
are on gh-pages, and the dmgs are on GitHub releases. Existing installs read
from those places, so the move is in three steps, and nobody has to reinstall.

Needful publishes to the `omarepo` deployment (`../pkgs.rails.quest`, served at
https://pkgs.lape.dev). `.omarepo.conf` (from `omarepo init`) holds Needful's
settings. Commit it, and it must be at the tag being published.

## 1. Reuse Needful's GPG key

The deployment's `GPG_KEY` (in `../pkgs.rails.quest/deployment.conf`) is already
the same fingerprint as `NEEDFUL_GPG_KEY`. Machines that already
trust it don't trust anything new, and Flatpak can follow a redirect without
importing another key.

## 2. Publish the next release to both places

Bump Needful as usual, then make one release that goes out the old way and
the new way:

1. In `../needful/bin/build-macos`, point the feed at the bucket:
   `SUFeedURL` = `https://pkgs.lape.dev/macos/needful/appcast.xml`. Copies that
   update to this build follow the new feed from then on.
2. Release from Needful as today (`bin/release-arch --publish`,
   `bin/release-flatpak --publish`, `bin/release-macos --publish`). Old copies on the Mac
   get the build that points to the new feed. The GitHub release now has the
   package and the dmg.
3. Add the Flatpak bundle to the release, since `bin/release-flatpak` doesn't upload
   one: `bin/flatpak` writes `build/needful.flatpak` from the checkout, so run it on
   the tagged commit, then run `gh release upload v<version> build/needful.flatpak`.
4. From here: `omarepo publish v<version> --push` in this checkout on the Linux box and again on
   the Mac. Or run the deployment's Publish workflow.

## 3. Point the old channels at the new ones

- **Flatpak** follows a redirect by itself. In Needful's gh-pages checkout
  (`build/pages`), run:

  ```sh
  flatpak build-update-repo --redirect-url=https://pkgs.lape.dev/flatpak/repo/ \
      --gpg-sign="$NEEDFUL_GPG_KEY" build/pages/flatpak/repo
  ```

  and push gh-pages. On their next `flatpak update`, installs move their
  `needful` remote to the bucket.
- **pacman** has no redirect. Machines replace the `[needful]` section in
  `/etc/pacman.conf` with the `[omarepo]` one from https://pkgs.lape.dev/. With
  the same key, there's nothing else to do. Until every machine has
  switched, keep running `bin/release-arch --publish`, or say so once in the release
  notes.
- **macOS** needs nothing more after step 2.

After that, Needful's `bin/release-*` scripts and gh-pages can go, and
`templates/app-release.yml` can replace them: it builds on your runners,
puts the files on the release, and asks the deployment to publish them. For Needful's build steps:

- **Arch**: what `bin/release-arch` does up to `extra-x86_64-build`. Export the tag with
  `git archive`, fill in `_tarball` and `_sha256`, and build in the chroot. Upload the package
  unsigned, because omarepo signs it.
- **Flatpak**: what `bin/release-flatpak` does up to `flatpak-builder`, including the
  metainfo release entry. Then `flatpak build-bundle` instead of `build-commit-from`.
- **macOS**: `bin/build-macos` with a Developer ID identity, then notarize and staple.
  Notarization needs the hardened runtime, which the script leaves off today because
  library validation refuses frameworks signed without a team. A Developer ID
  gives everything a team, so that reason goes away.
