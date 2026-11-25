import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A short menu where the pointer is, looking like the pickers' lists: each
// entry says what it does, with a tick where one is on, and a line between
// groups. Entries are { text, ticked, run } or { line: true }.
Popup {
    id: menu
    property var entries: []

    // Opens at (x, y) in that item, kept inside the window.
    function show(item, x, y, list) {
        entries = list
        const at = item.mapToItem(parent, x, y)
        menu.x = Math.max(8, Math.min(at.x, parent.width - width - 8))
        menu.y = Math.max(8, Math.min(at.y, parent.height - implicitHeight - 8))
        open()
    }

    parent: Overlay.overlay
    // Drawn in the window like everything else, as the pickers' lists are.
    popupType: Popup.Item
    width: 230
    padding: 4
    // Takes the keys while it is up, so Esc closes it, not what is behind.
    focus: true
    background: Rectangle {
        radius: Math.min(4, theme.radius)
        color: Ui.popup
        border.width: 1
        border.color: Ui.line
    }
    contentItem: Column {
        Repeater {
            model: menu.entries
            delegate: Item {
                id: entry
                required property var modelData
                width: menu.availableWidth
                height: modelData.line ? 9 : 30
                Rectangle {
                    visible: !!entry.modelData.line
                    y: 4
                    width: parent.width
                    height: 1
                    color: Ui.line
                }
                Rectangle {
                    visible: !entry.modelData.line
                    anchors.fill: parent
                    radius: Math.min(4, theme.radius)
                    color: hover.hovered ? Ui.fieldHover : "transparent"
                    Label {
                        anchors { left: parent.left; right: tick.left; leftMargin: 10; rightMargin: 6; verticalCenter: parent.verticalCenter }
                        text: entry.modelData.text || ""
                        font.pixelSize: Ui.body
                        color: Ui.text
                        elide: Text.ElideRight
                    }
                    Glyph {
                        id: tick
                        anchors { right: parent.right; rightMargin: 9; verticalCenter: parent.verticalCenter }
                        name: "check"
                        size: 14
                        color: theme.accent
                        visible: !!entry.modelData.ticked
                    }
                }
                HoverHandler { id: hover; enabled: !entry.modelData.line }
                TapHandler {
                    enabled: !entry.modelData.line
                    onTapped: {
                        const run = entry.modelData.run
                        menu.close()
                        run()
                    }
                }
            }
        }
    }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 90 } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 90 } }
}
