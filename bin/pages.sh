# Shared by the release scripts, which publish to the same gh-pages branch,
# each in its own folder, and to the same GitHub release for each tag. The
# binaries (dmgs, packages) go on the release; gh-pages keeps the text that
# points to them. Sourced, not run; set ROOT first.

GITHUB_REPO=kjlape/needful
PAGES_URL="${NEEDFUL_PAGES_URL:-https://kjlape.github.io/needful}"
RELEASES_URL="https://github.com/$GITHUB_REPO/releases"
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

# Sets VERSION and TAG from needful.pro, on a clean checkout, and SOURCE to
# the commit that is the release: TAG's, once it's made, so commits after it
# (a fix to these scripts, say) change how it's published but not what ships.
# ASSETS is where the release's binaries wait for --publish to upload them.
release_version() {
    [ -z "$(git -C "$ROOT" status --porcelain)" ] || die "commit or stash your changes first."
    VERSION="$(sed -n 's/^VERSION *= *//p' "$ROOT/needful.pro")"
    TAG="v$VERSION"
    ASSETS="$ROOT/build/release-assets/$TAG"
    SOURCE="$(git -C "$ROOT" rev-parse HEAD)"
    if git -C "$ROOT" rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
        tagged="$(git -C "$ROOT" rev-parse "$TAG^{commit}")"
        if [ "$tagged" != "$SOURCE" ]; then
            SOURCE="$tagged"
            echo "Releasing $TAG as tagged, from $(git -C "$ROOT" rev-parse --short "$SOURCE"), not this commit."
            echo "To ship this commit, bump the version."
        fi
    fi
}

# The release notes, one line each: commit subjects since the last release,
# oldest first.
release_subjects() {
    last="$(git -C "$ROOT" describe --tags --abbrev=0 --match 'v*' "$SOURCE^" 2>/dev/null || true)"
    subjects=""
    [ -z "$last" ] || subjects="$(git -C "$ROOT" log --reverse --format=%s "$last..$SOURCE")"
    printf '%s\n' "${subjects:-First release.}"
}

# The release notes as <li> lines.
release_notes() {
    release_subjects | sed 's/&/\&amp;/g; s/</\&lt;/g; s/>/\&gt;/g' | sed 's|.*|<li>&</li>|'
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

# Writes index.html from each platform's install.html (macos/, arch/,
# flatpak/), so a release for one platform keeps the others' instructions, then commits
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

# For a version gh-pages on origin already has: with --publish, finishes a
# publish that stopped after pushing gh-pages; without it, says so. Exits.
# The release's files went up before gh-pages did, so none are uploaded.
pages_finish() { # what
    [ "$PUBLISH" = 1 ] || die "$1 is already published. Bump the version to release another."
    echo "$1 is already on gh-pages; finishing its publish."
    pages_publish "$1"
    echo "Published."
    exit 0
}

# Uploads files to TAG's GitHub release, making the release if there isn't
# one yet. A file it already has is replaced: that's one from a publish that
# stopped before gh-pages pointed to it, and the gh-pages about to be pushed
# carries the signatures of this run's files, not that one's.
#
# Only bin/release-arch marks a release Latest (RELEASE_LATEST=1), since
# pacman finds its repository at releases/latest, and the Mac's release of a
# version can come before Arch's.
release_upload() { # files
    if ! gh release view "$TAG" -R "$GITHUB_REPO" >/dev/null 2>&1; then
        release_subjects | sed 's/^/- /' |
            gh release create "$TAG" -R "$GITHUB_REPO" --verify-tag --latest=false \
                --title "Needful $VERSION" --notes-file - >/dev/null
    fi
    for file in "$@"; do
        [ -f "$file" ] || die "$file is missing."
    done
    [ "$#" = 0 ] || gh release upload "$TAG" -R "$GITHUB_REPO" --clobber "$@"
    [ "${RELEASE_LATEST:-}" != 1 ] || gh release edit "$TAG" -R "$GITHUB_REPO" --latest >/dev/null
}

# Pushes the tag, uploads the files to its GitHub release, then pushes
# gh-pages, which points to them, and turns on GitHub Pages the first time.
pages_publish() { # what, for the question, then the release's files
    what="$1"
    shift
    command -v gh >/dev/null || die "gh is not installed. Install github-cli, then gh auth login."
    question="Push $TAG"
    [ "$#" = 0 ] || question="$question, upload $# files to its GitHub release"
    confirm "$question, replace gh-pages on origin with $what, and serve it on GitHub Pages?"
    git -C "$ROOT" push -q origin "$TAG"
    release_upload "$@"
    git -C "$PAGES" push -q --force origin gh-pages
    # A public repository turns Pages on by itself when gh-pages is first
    # pushed, sometimes between a check and this request, so "already
    # enabled" (409) counts as on.
    if ! gh api "repos/$GITHUB_REPO/pages" >/dev/null 2>&1; then
        if output="$(gh api -X POST "repos/$GITHUB_REPO/pages" -f "source[branch]=gh-pages" -f "source[path]=/" 2>&1)"; then
            echo "Turned on GitHub Pages; the first deploy takes a minute or two."
        else
            case "$output" in *409*) ;; *) die "couldn't turn on GitHub Pages: $output" ;; esac
        fi
    fi
}
