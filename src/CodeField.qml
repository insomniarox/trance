import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A block of preset code. It applies itself a moment after typing stops, so
// the picture follows along; a mistake shows underneath and the last
// working version keeps running.
ColumnLayout {
    id: block
    property string path
    property string label
    property string hint
    // What this block is for, shown when the pointer rests on its name.
    property string help
    readonly property bool edited: { backend.revision; return backend.isEdited(block.path) }
    spacing: 4
    Layout.topMargin: 4

    readonly property var current: { backend.revision; return backend.param(block.path) }
    readonly property string problem: { backend.problems; return backend.problemAt(block.path) }
    onCurrentChanged: if (!edit.activeFocus) edit.text = current === undefined ? "" : String(current)
    Component.onCompleted: edit.text = current === undefined ? "" : String(current)

    RowLayout {
        Layout.fillWidth: true
        Label {
            id: name
            text: block.label
            font.family: Ui.mono
            font.pixelSize: Ui.body
            font.weight: block.edited ? Font.DemiBold : Font.Normal
            color: block.edited ? theme.accent : Ui.text
            HoverHandler {
                enabled: block.help.length > 0
                onHoveredChanged: hovered ? win.showHelp(name, block.label, block.help, "") : win.hideHelp(name)
            }
        }
        Label {
            Layout.fillWidth: true
            text: block.hint
            elide: Text.ElideRight
            color: Ui.textFaint
            font.pixelSize: Ui.small
        }
    }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: Math.max(52, edit.implicitHeight + 16)
        radius: Math.min(4, theme.radius)
        color: edit.activeFocus ? Ui.fieldActive : Ui.field
        border.width: 1
        border.color: block.problem.length > 0 ? Ui.danger : edit.activeFocus ? theme.accent : "transparent"
        TextEdit {
            id: edit
            anchors.fill: parent
            anchors.margins: 8
            color: Ui.text
            selectionColor: theme.accent
            selectedTextColor: theme.accentForeground
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            font.family: Ui.mono
            font.pixelSize: Ui.body
            onTextChanged: if (activeFocus) apply.restart()
            onActiveFocusChanged: if (!activeFocus && apply.running) { apply.stop(); backend.setParam(block.path, text) }
            Timer { id: apply; interval: 450; onTriggered: backend.setParam(block.path, edit.text) }
        }
    }
    Label {
        visible: block.problem.length > 0
        text: block.problem
        color: Ui.danger
        wrapMode: Text.WordWrap
        font.pixelSize: Ui.small
        Layout.fillWidth: true
    }
}
