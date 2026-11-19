QT += core gui quick quickcontrols2 network testlib
CONFIG += c++17 testcase
TARGET = needful_tests
TEMPLATE = app
DEFINES += APP_VERSION=\\\"test\\\"

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/cli.h ../src/httpserver.h ../src/serve.h ../src/store.h ../src/sync.h ../src/theme.h \
           ../src/translator.h ../src/universes.h ../src/webapi.h
SOURCES += needful_tests.cpp ../src/backend.cpp ../src/cli.cpp ../src/httpserver.cpp ../src/serve.cpp ../src/store.cpp ../src/sync.cpp \
           ../src/theme.cpp ../src/translator.cpp ../src/universes.cpp ../src/webapi.cpp
RESOURCES += ../src/web/web.qrc
