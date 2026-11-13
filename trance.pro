QT += core gui qml quick quickcontrols2 opengl dbus

CONFIG += c++17 release
TARGET = trance
TEMPLATE = app

HEADERS += \
    src/audio.h \
    src/audiocapture.h \
    src/backend.h \
    src/cli.h \
    src/engine.h \
    src/expr.h \
    src/library.h \
    src/nowplaying.h \
    src/preset.h \
    src/theme.h \
    src/translator.h \
    src/visualizeritem.h

SOURCES += \
    src/main.cpp \
    src/audio.cpp \
    src/audiocapture.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/engine.cpp \
    src/expr.cpp \
    src/library.cpp \
    src/nowplaying.cpp \
    src/preset.cpp \
    src/presetdocs.cpp \
    src/theme.cpp \
    src/translator.cpp \
    src/visualizeritem.cpp

RESOURCES += src/resources.qrc
