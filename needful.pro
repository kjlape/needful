QT += core gui qml quick quickcontrols2 network

CONFIG += c++17 release
TARGET = needful
TEMPLATE = app
VERSION = 0.1.0
DEFINES += APP_VERSION=\\\"$$VERSION\\\"

HEADERS += \
    src/backend.h \
    src/cli.h \
    src/httpserver.h \
    src/serve.h \
    src/store.h \
    src/sync.h \
    src/theme.h \
    src/translator.h \
    src/webapi.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/httpserver.cpp \
    src/serve.cpp \
    src/store.cpp \
    src/sync.cpp \
    src/theme.cpp \
    src/translator.cpp \
    src/webapi.cpp

RESOURCES += src/resources.qrc src/web/web.qrc
