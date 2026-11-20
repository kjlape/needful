# The link library (Rust), built by cargo and linked in statically. Make only
# runs cargo when a Rust input (sources, manifest, lockfile) changed; cargo's
# own fingerprints keep unchanged crates cached. CFLAGS is pinned to a constant
# so the fingerprint doesn't flip-flop between bin/build and makepkg (ambient
# flags differ): a changed CFLAGS recompiles ring's C and everything downstream.
# It must stay without LTO: makepkg's -flto would make the C bitcode the Rust
# archive can't index, and the link fails.
LINK_DIR = $$PWD
LINK_LIB = $$LINK_DIR/target/release/libneedful_link.a

needful_link.target = $$LINK_LIB
needful_link.commands = CFLAGS=\"-fno-lto\" cargo build --release --locked --manifest-path $$LINK_DIR/Cargo.toml
needful_link.depends = $$LINK_DIR/Cargo.toml $$LINK_DIR/Cargo.lock $$LINK_DIR/src/lib.rs
QMAKE_EXTRA_TARGETS += needful_link
PRE_TARGETDEPS += $$LINK_LIB

LIBS += $$LINK_LIB
unix:!macx: LIBS += -ldl -lpthread -lm
macx: LIBS += -framework Security -framework CoreFoundation -framework SystemConfiguration
