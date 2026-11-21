import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The controls that come up over the picture while the pointer is moving,
// for doing by mouse what the keys do. Each button says its key.
Rectangle {
    id: bar
    property bool fullScreen: false
    signal browse()
    signal previous()
    signal random()
    signal next()
    signal edit()
    signal settings()
    signal help()
    signal toggleFullScreen()
    readonly property alias hovered: hover.hovered

    function hint(text, key) { return text + "  (" + key + ")" }

    width: row.implicitWidth + 12
    height: 42
    radius: Math.min(21, theme.radius)
    color: "#d0101014"
    border.color: Ui.line

    HoverHandler { id: hover }
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }

    component Divider: Rectangle {
        Layout.preferredWidth: 1
        Layout.preferredHeight: 18
        Layout.leftMargin: 4
        Layout.rightMargin: 4
        color: Ui.line
    }

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 2
        IconButton { glyph: "list"; tip: bar.hint(qsTr("Browse presets"), "P"); onClicked: bar.browse() }
        Divider {}
        IconButton { glyph: "previous"; tip: bar.hint(qsTr("Previous preset"), "Backspace"); onClicked: bar.previous() }
        IconButton { glyph: "shuffle"; tip: bar.hint(qsTr("Random preset"), "R"); onClicked: bar.random() }
        IconButton { glyph: "next"; tip: bar.hint(qsTr("Next preset"), "Space"); onClicked: bar.next() }
        Divider {}
        IconButton { glyph: "edit"; tip: bar.hint(qsTr("Edit this preset"), "E"); onClicked: bar.edit() }
        IconButton { glyph: "settings"; tip: bar.hint(qsTr("Settings"), "S"); onClicked: bar.settings() }
        IconButton { glyph: "help"; tip: bar.hint(qsTr("Shortcuts"), "?"); onClicked: bar.help() }
        Divider {}
        IconButton {
            glyph: bar.fullScreen ? "windowed" : "fullscreen"
            tip: bar.hint(qsTr("Full screen"), "F")
            onClicked: bar.toggleFullScreen()
        }
    }
}
