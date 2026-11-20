# The link library (Rust), built by cargo and linked in statically. Cargo
# decides whether anything changed, so it runs on every make. The C it
# compiles (ring's) must stay real object code: with makepkg's -flto it would
# be LTO bitcode that the Rust archive can't index, and the link fails.
LINK_DIR = $$PWD
LINK_LIB = $$LINK_DIR/target/release/libneedful_link.a

needful_link.target = $$LINK_LIB
needful_link.commands = CFLAGS=\"$${LITERAL_DOLLAR}$${LITERAL_DOLLAR}CFLAGS -fno-lto\" cargo build --release --locked --manifest-path $$LINK_DIR/Cargo.toml
needful_link.depends = FORCE
QMAKE_EXTRA_TARGETS += needful_link
PRE_TARGETDEPS += $$LINK_LIB

LIBS += $$LINK_LIB
unix:!macx: LIBS += -ldl -lpthread -lm
macx: LIBS += -framework Security -framework CoreFoundation -framework SystemConfiguration
