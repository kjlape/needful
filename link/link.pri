# The link library (Rust), built by cargo and linked in statically. Cargo
# decides whether anything changed, so it runs on every make.
LINK_DIR = $$PWD
LINK_LIB = $$LINK_DIR/target/release/libneedful_link.a

needful_link.target = $$LINK_LIB
needful_link.commands = cargo build --release --locked --manifest-path $$LINK_DIR/Cargo.toml
needful_link.depends = FORCE
QMAKE_EXTRA_TARGETS += needful_link
PRE_TARGETDEPS += $$LINK_LIB

LIBS += $$LINK_LIB
unix:!macx: LIBS += -ldl -lpthread -lm
macx: LIBS += -framework Security -framework CoreFoundation -framework SystemConfiguration
