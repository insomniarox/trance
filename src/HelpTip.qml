import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// The explanation that comes up when the pointer rests on a setting's
// name: what it is called, what it does, and what it is called in the file.
// One of these serves the whole window; whatever is hovered borrows it.
Popup {
    id: tip
    property string title
    property string body
    property string detail
    // The item it is showing for, so a later item can't close it by leaving.
    property Item owner: null

    function show(item, title, body, detail) {
        owner = item
        tip.title = title
        tip.body = body
        tip.detail = detail
        wait.restart()
    }
    function hide(item) {
        if (owner !== item)
            return
        wait.stop()
        owner = null
        close()
    }

    popupType: Popup.Item
    closePolicy: Popup.NoAutoClose
    modal: false
    focus: false
    width: 340
    padding: 12
    margins: 8
    parent: owner
    y: owner ? owner.height + 6 : 0
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 90 } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 90 } }

    Timer { id: wait; interval: 450; onTriggered: if (tip.owner) tip.open() }

    background: Rectangle {
        radius: Math.min(4, theme.radius)
        color: Ui.popup
        border.width: 1
        border.color: Ui.line
    }
    contentItem: Column {
        spacing: 5
        Text {
            width: parent.width
            text: tip.title
            color: Ui.text
            font.pixelSize: Ui.body
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
        }
        Text {
            width: parent.width
            visible: text.length > 0
            text: tip.body
            color: Ui.textDim
            font.pixelSize: Ui.small
            lineHeight: 1.15
            wrapMode: Text.WordWrap
        }
        Text {
            width: parent.width
            visible: text.length > 0
            text: tip.detail
            color: Ui.textFaint
            font.family: Ui.mono
            font.pixelSize: Ui.tiny
            wrapMode: Text.WordWrap
        }
    }
}
