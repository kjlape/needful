QT += core gui qml quick quickcontrols2

CONFIG += c++17 release
TARGET = needful
TEMPLATE = app
VERSION = 0.1.0
DEFINES += APP_VERSION=\\\"$$VERSION\\\"

HEADERS += \
    src/backend.h \
    src/cli.h \
    src/store.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/store.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc

