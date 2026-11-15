QT += core gui qml quick quickcontrols2 opengl network

CONFIG += c++17 release
TARGET = trance
TEMPLATE = app

# The one place the version is written. A release is tagged v and this, and
# the Windows installer and the program's file details take it from here.
VERSION = 0.1.0
DEFINES += TRANCE_VERSION=\\\"$$VERSION\\\"

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
    src/remote.h \
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
    src/preset.cpp \
    src/presetdocs.cpp \
    src/remote.cpp \
    src/shaderworker.cpp \
    src/theme.cpp \
    src/visualizeritem.cpp

# What differs by system: how the music is heard, who says what is playing,
# and the desktop the corners are taken from.
unix {
    QT += dbus
    SOURCES += \
        src/audiocapture_pipewire.cpp \
        src/nowplaying_mpris.cpp \
        src/theme_hyprland.cpp
}

win32 {
    SOURCES += \
        src/audiocapture_wasapi.cpp \
        src/nowplaying_smtc.cpp
    # What Windows' sound is reached through, and its media controls.
    LIBS += -lole32 -lmmdevapi -lavrt -loleaut32 -lruntimeobject
    # C++20 here, for C++/WinRT, which the media controls are asked through:
    # under C++17 it leans on a coroutine header Microsoft is taking away.
    CONFIG += c++20
    # A console program, so that a shell waits for a command and gets its
    # output; the manifest keeps Windows from opening a console for the window.
    CONFIG += console
    CONFIG -= debug_and_release
    # Every core, which Microsoft's compiler keeps to one of unless asked.
    CONFIG += msvc_mp
    QMAKE_LFLAGS += /MANIFEST:embed /MANIFESTINPUT:$$shell_quote($$shell_path($$PWD/pkgbuild/trance.manifest))
    RC_ICONS = pkgbuild/trance.ico
    # What Explorer's Details tab and Task Manager call the program.
    QMAKE_TARGET_PRODUCT = Trance
    QMAKE_TARGET_DESCRIPTION = Trance
    QMAKE_TARGET_COMPANY = insomniarox
    QMAKE_TARGET_COPYRIGHT = Copyright (c) 2026 insomniarox
    # M_PI and its like, which Microsoft's <cmath> keeps back unless asked,
    # and no min and max macros from its windows.h.
    DEFINES += _USE_MATH_DEFINES NOMINMAX
}

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
