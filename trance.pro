QT += core gui qml quick quickcontrols2 opengl dbus network

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
    src/shaderworker.h \
    src/theme.h \
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
    src/shaderworker.cpp \
    src/theme.cpp \
    src/visualizeritem.cpp

RESOURCES += src/resources.qrc

# Material.roundedScale, which the corners are drawn with, came in Qt 6.5.
!versionAtLeast(QT_VERSION, 6.5.0): error("Trance needs Qt 6.5 or newer; this is Qt $$QT_VERSION.")

# make install, for where there is no package: qmake6 PREFIX=/usr to move it.
isEmpty(PREFIX): PREFIX = /usr/local
target.path = $$PREFIX/bin
desktop.files = pkgbuild/trance.desktop
desktop.path = $$PREFIX/share/applications
icon.files = pkgbuild/trance.svg
icon.path = $$PREFIX/share/icons/hicolor/scalable/apps
INSTALLS += target desktop icon
