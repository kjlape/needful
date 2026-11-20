# Notes for coding agents

## Versioning

Bump the version with each change worth shipping, roughly following semver:

- Patch (0.2.0 → 0.2.1) for fixes and small changes that add nothing new.
- Minor (0.2.0 → 0.3.0) for new features, commands, or options.
- Major for changes that break existing data, commands, or setups. Until
  1.0, a minor bump is enough for these; say so in the commit message.

The version lives in two places; change both together:

- `needful.pro` (`VERSION`), which the build and `bin/build-macos` read
- `pkgbuild/PKGBUILD` (`pkgver`; reset `pkgrel` to 1 when `pkgver` changes)

`link/Cargo.toml` versions the internal link library, not the app; leave it.
