import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A question in the middle of the window, drawn like the menus and the
// pickers' lists. The picture stays in full view behind it: nothing is
// dimmed, though the rest of the window waits for an answer. The buttons go
// in `buttons`, the one it asks for first.
Dialog {
    id: dialog
    default property alias content: body.data
    property alias buttons: buttonRow.data

    parent: Overlay.overlay
    anchors.centerIn: parent
    // A set width. Left to size itself, a dialog whose text is wider than
    // its row of buttons chases its own button box, which Qt reports as a
    // binding loop every time the text changes.
    width: Math.min(420, (parent ? parent.width : 468) - 48)
    modal: true
    // Drawn in the window like everything else, as the menus are.
    popupType: Popup.Item
    Overlay.modal: Item {}
    padding: Ui.pad
    topPadding: 10

    background: Rectangle {
        radius: Math.min(4, theme.radius)
        color: Ui.popup
        border.width: 1
        border.color: Ui.line
    }
    header: Label {
        visible: dialog.title.length > 0
        text: dialog.title
        padding: Ui.pad
        bottomPadding: 0
        font.pixelSize: Ui.title
        font.weight: Font.DemiBold
        color: Ui.text
        elide: Text.ElideRight
    }
    contentItem: Column { id: body }
    footer: Item {
        implicitHeight: buttonRow.implicitHeight + Ui.pad
        RowLayout {
            id: buttonRow
            anchors { right: parent.right; rightMargin: Ui.pad; top: parent.top }
            spacing: 8
        }
    }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 90 } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 90 } }
}
