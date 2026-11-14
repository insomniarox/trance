import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A panel down one side of the window: a heading that says what it is and
// what it covers, a way to close it, and the contents underneath.
Rectangle {
    id: panel
    property string title
    // A few words on what the panel's contents apply to.
    property string scope
    // The side the picture is on, where the dividing line goes.
    property bool pictureOnLeft: true
    default property alias contents: body.data
    // Anything extra for the heading, before the close button.
    property alias tools: tools.data
    signal closeRequested()

    color: Ui.panel

    // Swallows clicks and scrolling so they don't reach the picture.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: function (wheel) { wheel.accepted = true } }

    Rectangle {
        width: 1
        color: Ui.line
        anchors { top: parent.top; bottom: parent.bottom }
        x: panel.pictureOnLeft ? 0 : parent.width - 1
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Ui.pad
            Layout.rightMargin: 8
            Layout.preferredHeight: 52
            spacing: 10
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label {
                    Layout.fillWidth: true
                    text: panel.title
                    font.pixelSize: Ui.title
                    font.weight: Font.DemiBold
                    color: Ui.text
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: panel.scope
                    font.pixelSize: Ui.tiny
                    color: Ui.textFaint
                    elide: Text.ElideRight
                }
            }
            Row { id: tools; spacing: 2; Layout.alignment: Qt.AlignVCenter }
            IconButton { glyph: "close"; tip: qsTr("Close"); onClicked: panel.closeRequested() }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Ui.line }
        Item { id: body; Layout.fillWidth: true; Layout.fillHeight: true }
    }
}
