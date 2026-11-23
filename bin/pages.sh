# Shared by bin/release-macos and bin/release-arch, which publish to the same
# gh-pages branch, each in its own folder. Sourced, not run; set ROOT first.

GITHUB_REPO=kjlape/needful
PAGES_URL="${NEEDFUL_PAGES_URL:-https://kjlape.github.io/needful}"
PAGES="$ROOT/build/pages"

die() {
    echo "error: $*" >&2
    exit 1
}

# Asks before anything other people will see. NEEDFUL_YES=1 answers yes.
confirm() {
    [ "${NEEDFUL_YES:-}" = 1 ] && return 0
    printf '%s [y/N] ' "$1"
    read -r answer
    case "$answer" in y | Y | yes) return 0 ;; *) die "stopped" ;; esac
}

# Sets PUBLISH from the script's one optional argument, or prints the
# script's header comment and exits.
parse_publish() {
    PUBLISH=0
    case "${1:-}" in
        "") ;;
        --publish) PUBLISH=1 ;;
        *) sed -n '2,/^[^#]/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'; exit 1 ;;
    esac
}

# Sets VERSION and TAG from needful.pro, on a clean checkout whose tag, if
# it's made yet, is this commit.
release_version() {
    [ -z "$(git -C "$ROOT" status --porcelain)" ] || die "commit or stash your changes first."
    VERSION="$(sed -n 's/^VERSION *= *//p' "$ROOT/needful.pro")"
    TAG="v$VERSION"
    if git -C "$ROOT" rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
        [ "$(git -C "$ROOT" rev-parse "$TAG^{commit}")" = "$(git -C "$ROOT" rev-parse HEAD)" ] ||
            die "$TAG already tags another commit. Bump the version first."
    fi
}

# The release notes as <li> lines: commit subjects since the last release,
# oldest first.
release_notes() {
    last="$(git -C "$ROOT" describe --tags --abbrev=0 --match 'v*' HEAD^ 2>/dev/null || true)"
    notes=""
    [ -z "$last" ] || notes="$(git -C "$ROOT" log --reverse --format=%s "$last"..HEAD |
        sed 's/&/\&amp;/g; s/</\&lt;/g; s/>/\&gt;/g' | sed 's|.*|<li>&</li>|')"
    printf '%s\n' "${notes:-<li>First release.</li>}"
}

# Makes build/pages a checkout of the gh-pages branch as published, so each
# release adds to what's there. Sets REMOTE_PAGES to 1 when origin has one.
# Bookkeeping on this branch uses commands that run no git hooks, so a
# broken global hook can't stop a release halfway.
pages_checkout() {
    REMOTE_PAGES=0
    git -C "$ROOT" ls-remote --exit-code origin refs/heads/gh-pages >/dev/null 2>&1 && REMOTE_PAGES=1
    if [ ! -e "$PAGES/.git" ]; then
        git -C "$ROOT" worktree prune
        if [ "$REMOTE_PAGES" = 1 ]; then
            git -C "$ROOT" fetch -q origin "+gh-pages:refs/remotes/origin/gh-pages"
            git -C "$ROOT" worktree add -q --no-checkout -B gh-pages "$PAGES" origin/gh-pages
            git -C "$PAGES" reset -q --hard
        else
            git -C "$ROOT" worktree add -q --orphan -b gh-pages "$PAGES"
        fi
    fi
    git -C "$PAGES" symbolic-ref HEAD refs/heads/gh-pages
    if [ "$REMOTE_PAGES" = 1 ]; then
        git -C "$PAGES" fetch -q origin gh-pages
        git -C "$PAGES" reset -q --hard FETCH_HEAD
    fi
}

# Writes index.html from each platform's install.html (macos/, arch/), so
# a release for one platform keeps the other's instructions, then commits
# the whole branch as one parentless commit, so dropped downloads don't stay
# in its history. Tags this commit TAG.
pages_commit() { # message
    cp "$ROOT/pkgbuild/needful.svg" "$PAGES/needful.svg"
    # GitHub Pages would otherwise run Jekyll over the files.
    touch "$PAGES/.nojekyll"
    {
        cat <<EOF
<!doctype html>
<html lang="en">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Needful</title>
<style>
  body { font: 16px/1.5 system-ui, sans-serif; max-width: 42rem; margin: 3rem auto; padding: 0 1rem; color: #1a1a1a; background: #fff; }
  pre { background: #f2f2f2; padding: .75rem 1rem; overflow-x: auto; }
  img { width: 64px; height: 64px; }
  @media (prefers-color-scheme: dark) { body { color: #e6e6e6; background: #0e0e10; } pre { background: #1c1c20; } a { color: #ffd60a; } }
</style>
<img src="needful.svg" alt="">
<h1>Needful</h1>
<p>What needs doing, most needful first.</p>
EOF
        for part in "$PAGES"/*/install.html; do
            [ -f "$part" ] && cat "$part"
        done
        echo "</html>"
    } >"$PAGES/index.html"
    git -C "$PAGES" add -A
    tree="$(git -C "$PAGES" write-tree)"
    commit="$(git -C "$PAGES" commit-tree "$tree" -m "$1")"
    git -C "$PAGES" update-ref refs/heads/gh-pages "$commit"
    git -C "$ROOT" rev-parse -q --verify "refs/tags/$TAG" >/dev/null || git -C "$ROOT" tag "$TAG"
}

# Pushes the tag and gh-pages, and turns on GitHub Pages the first time.
pages_publish() { # what, for the question
    command -v gh >/dev/null || die "gh is not installed. Install github-cli, then gh auth login."
    confirm "Push $TAG, replace gh-pages on origin with $1, and serve it on GitHub Pages?"
    git -C "$ROOT" push -q origin "$TAG"
    git -C "$PAGES" push -q --force origin gh-pages
    if ! gh api "repos/$GITHUB_REPO/pages" >/dev/null 2>&1; then
        gh api -X POST "repos/$GITHUB_REPO/pages" -f "source[branch]=gh-pages" -f "source[path]=/" >/dev/null
        echo "Turned on GitHub Pages; the first deploy takes a minute or two."
    fi
}
