QT += core gui quick quickcontrols2 opengl testlib dbus network
CONFIG += c++17 testcase
TARGET = trance_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/audio.h ../src/audiocapture.h ../src/backend.h ../src/engine.h ../src/expr.h \
    ../src/library.h ../src/preset.h ../src/theme.h
SOURCES += trance_tests.cpp ../src/audio.cpp ../src/audiocapture.cpp ../src/backend.cpp ../src/engine.cpp \
    ../src/expr.cpp ../src/library.cpp ../src/preset.cpp ../src/presetdocs.cpp ../src/theme.cpp

RESOURCES += ../src/resources.qrc
