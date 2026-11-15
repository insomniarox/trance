QT += core gui quick quickcontrols2 opengl testlib network
CONFIG += c++17 testcase
TARGET = trance_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/audio.h ../src/audiocapture.h ../src/backend.h ../src/engine.h ../src/expr.h \
    ../src/library.h ../src/preset.h ../src/remote.h ../src/theme.h
SOURCES += trance_tests.cpp ../src/audio.cpp ../src/audiocapture.cpp ../src/backend.cpp ../src/engine.cpp \
    ../src/expr.cpp ../src/library.cpp ../src/preset.cpp ../src/presetdocs.cpp ../src/remote.cpp ../src/theme.cpp

unix: SOURCES += ../src/audiocapture_pipewire.cpp ../src/theme_hyprland.cpp
win32 {
    HEADERS += ../src/nowplaying.h
    SOURCES += ../src/audiocapture_wasapi.cpp ../src/nowplaying_smtc.cpp
    LIBS += -lole32 -lmmdevapi -lavrt -loleaut32 -lruntimeobject -luser32 -lshell32
    # As in trance.pro: C++/WinRT wants C++20 of Microsoft's compiler.
    CONFIG += c++20
    CONFIG -= debug_and_release
    # Every core, which Microsoft's compiler keeps to one of unless asked.
    CONFIG += msvc_mp
    DEFINES += _USE_MATH_DEFINES NOMINMAX
}

RESOURCES += ../src/resources.qrc

# Where the README is, so the counts it gives can be checked.
DEFINES += TRANCE_ROOT=\\\"$$clean_path($$PWD/..)\\\"
