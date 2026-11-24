QT += core gui qml quick quickcontrols2 network

CONFIG += c++17 release
TARGET = needful
TEMPLATE = app
VERSION = 0.15.0
DEFINES += APP_VERSION=\\\"$$VERSION\\\"

HEADERS += \
    src/backend.h \
    src/cli.h \
    src/devices.h \
    src/members.h \
    src/link.h \
    src/peerpanel.h \
    src/peers.h \
    src/httpserver.h \
    src/serve.h \
    src/store.h \
    src/sync.h \
    src/theme.h \
    src/translator.h \
    src/universes.h \
    src/webapi.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/devices.cpp \
    src/members.cpp \
    src/peerpanel.cpp \
    src/peers.cpp \
    src/httpserver.cpp \
    src/serve.cpp \
    src/store.cpp \
    src/sync.cpp \
    src/theme.cpp \
    src/translator.cpp \
    src/universes.cpp \
    src/webapi.cpp

RESOURCES += src/resources.qrc src/web/web.qrc

include(link/link.pri)

# The bundle ID the launchd agents are named after (kjlape.needful.serve).
macx: QMAKE_TARGET_BUNDLE_PREFIX = kjlape

# Updates for the macOS app. bin/build-macos turns this on, with SPARKLE_DIR
# pointing at the Sparkle release it downloaded.
macx:sparkle {
    isEmpty(SPARKLE_DIR): error("CONFIG+=sparkle needs SPARKLE_DIR, the folder holding Sparkle.framework")
    DEFINES += NEEDFUL_SPARKLE
    HEADERS += src/updater.h
    OBJECTIVE_SOURCES += src/updater_mac.mm
    QMAKE_CXXFLAGS += -F$$SPARKLE_DIR
    QMAKE_OBJECTIVE_CFLAGS += -F$$SPARKLE_DIR -fobjc-arc
    LIBS += -F$$SPARKLE_DIR -framework Sparkle -framework AppKit
    QMAKE_LFLAGS += -Wl,-rpath,@executable_path/../Frameworks
}
