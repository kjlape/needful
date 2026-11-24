# Shared by bin/flatpak and bin/release-flatpak. Sourced, not run; set ROOT
# first, and source bin/pages.sh for die.

APP=io.github.kjlape.Needful
FLATPAK_RUNTIME=org.kde.Platform//6.11
FLATPAK_SDK=org.kde.Sdk//6.11
# The Rust extension's branch is the freedesktop SDK the KDE one is built on.
FLATPAK_RUST=org.freedesktop.Sdk.Extension.rust-stable//25.08

# Sets BUILDER. The runtime, SDK and builder come from the host's flatpak,
# which can always see Flathub; the sandboxed builder sometimes can't.
setup_builder() {
    command -v flatpak >/dev/null || die "flatpak is not installed. sudo pacman -S flatpak"
    flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
    flatpak install --user -y --noninteractive flathub "$FLATPAK_RUNTIME" "$FLATPAK_SDK" "$FLATPAK_RUST" org.flatpak.Builder
    # In its sandbox "user" means its own data directory; point it at ours.
    user_dir="${XDG_DATA_HOME:-$HOME/.local/share}/flatpak"
    BUILDER="flatpak run --env=FLATPAK_USER_DIR=$user_dir --command=flatpak-builder org.flatpak.Builder"
}

# The builder runs in its own sandbox with a private /tmp, so a checkout
# there is invisible to it and the build fails with a confusing path.
refuse_tmp() { # path
    case "$1" in /tmp/* | /var/tmp/*) die "build from a checkout under your home directory, not $1." ;; esac
}
