pragma Singleton
import QtQuick

// The look every panel shares: one set of surfaces, text strengths and
// sizes, so the browser, the editor, the settings and the overlays read as
// one app. The accent and the corner radius are not here; those follow the
// desktop and come from `theme`.
QtObject {
    // Panels sit over the picture, so they are dark whatever the theme.
    readonly property color panel: "#f40f0f13"
    readonly property color popup: "#ff1a1a20"
    readonly property color scrim: "#c8000000"
    readonly property color card: "#0cffffff"
    readonly property color field: "#16ffffff"
    readonly property color fieldHover: "#1fffffff"
    readonly property color fieldActive: "#2cffffff"
    readonly property color hover: "#14ffffff"
    readonly property color line: "#1cffffff"

    readonly property color text: "#f0ffffff"
    readonly property color textDim: "#a8ffffff"
    readonly property color textFaint: "#70ffffff"
    readonly property color danger: "#ff8a8a"
    readonly property color warning: "#e0c070"

    readonly property int title: 15
    readonly property int body: 13
    readonly property int small: 12
    readonly property int tiny: 11

    readonly property int control: 30   // the height of a field, list or button
    readonly property int row: 44       // the least height of a line of settings
    readonly property int pad: 16       // from a panel's edge to what is in it
    readonly property string mono: "monospace"
}
