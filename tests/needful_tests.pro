QT += core gui quick quickcontrols2 testlib
CONFIG += c++17 testcase
TARGET = needful_tests
TEMPLATE = app
DEFINES += APP_VERSION=\\\"test\\\"

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/store.h ../src/theme.h ../src/translator.h
SOURCES += needful_tests.cpp ../src/backend.cpp ../src/store.cpp ../src/theme.cpp ../src/translator.cpp
