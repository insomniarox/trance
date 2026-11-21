import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Every shortcut, shown with ?.
Rectangle {
    id: overlay
    color: Ui.scrim
    signal dismissed()

    // Keys are listed one cap each: a combination, or keys that do the same.
    readonly property var groups: [
        { title: qsTr("Presets"), keys: [
            [["Space"], qsTr("Next preset")],
            [["Backspace"], qsTr("Previous preset")],
            [["R"], qsTr("Random preset")],
            [["X"], qsTr("Mash up a new preset from parts of others")],
            [["L"], qsTr("Hold this preset, or let it change again")],
            [["P"], qsTr("Browse presets")],
            [["/"], qsTr("Search presets by name")]
        ] },
        { title: qsTr("Editing"), keys: [
            [["E"], qsTr("Edit this preset")],
            [["Ctrl", "Tab"], qsTr("Next part of the editor")],
            [["Ctrl", "S"], qsTr("Save the preset")],
            [["Ctrl", "Shift", "S"], qsTr("Save it under a new name")],
            [["Ctrl", "N"], qsTr("New preset")],
            [["Ctrl", "Z"], qsTr("Undo")],
            [["Ctrl", "Shift", "Z"], qsTr("Redo")]
        ] },
        { title: qsTr("Picture"), keys: [
            [["T"], qsTr("Change the colors: theme, palettes, or the preset's own")],
            [["G"], qsTr("Glass: let the desktop show through")],
            [["N"], qsTr("Track titles: in the corner, in the picture, or off")],
            [["D"], qsTr("Debug overlay: preset, frame rate and load, sound and levels")],
            [["F"], qsTr("Full screen")]
        ] },
        { title: qsTr("Sound"), keys: [
            [["M"], qsTr("Listen to the microphone instead")],
            [["+", "−"], qsTr("More or less sensitive to the music")]
        ] },
        { title: qsTr("Window"), keys: [
            [["S"], qsTr("Settings")],
            [["Esc"], qsTr("Close a panel, or leave full screen")],
            [["?"], qsTr("Show or hide this")],
            [["A"], qsTr("About Trance: version, system and files")],
            [["Q"], qsTr("Quit")]
        ] }
    ]

    MouseArea { anchors.fill: parent; onClicked: overlay.dismissed() }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 820)
        height: Math.min(parent.height - 48, columns.implicitHeight + 110)
        radius: theme.radius
        color: Ui.popup
        border.color: Ui.line

        MouseArea { anchors.fill: parent }

        Label {
            id: heading
            anchors { top: parent.top; left: parent.left; topMargin: 22; leftMargin: 28 }
            text: qsTr("Shortcuts")
            font.pixelSize: 18
            font.weight: Font.DemiBold
            color: Ui.text
        }
        IconButton {
            anchors { verticalCenter: heading.verticalCenter; right: parent.right; rightMargin: 16 }
            glyph: "close"
            onClicked: overlay.dismissed()
        }

        Flickable {
            anchors { top: heading.bottom; left: parent.left; right: parent.right; bottom: parent.bottom; margins: 28; topMargin: 18 }
            contentHeight: columns.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            // Two columns of groups when there is room for them.
            GridLayout {
                id: columns
                width: parent.width
                columns: width > 600 ? 2 : 1
                columnSpacing: 36
                rowSpacing: 22
                Repeater {
                    model: overlay.groups
                    ColumnLayout {
                        id: group
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredWidth: 1
                        spacing: 7
                        Label {
                            text: group.modelData.title
                            font.pixelSize: Ui.tiny
                            font.weight: Font.DemiBold
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: 0.8
                            color: Ui.textDim
                            Layout.bottomMargin: 2
                        }
                        Repeater {
                            model: group.modelData.keys
                            RowLayout {
                                id: line
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 12
                                Label {
                                    Layout.fillWidth: true
                                    text: line.modelData[1]
                                    font.pixelSize: Ui.body
                                    color: Ui.text
                                    wrapMode: Text.WordWrap
                                }
                                Row {
                                    spacing: 4
                                    Layout.alignment: Qt.AlignTop
                                    Repeater {
                                        model: line.modelData[0]
                                        Rectangle {
                                            required property string modelData
                                            width: Math.max(24, cap.implicitWidth + 14)
                                            height: 22
                                            radius: Math.min(4, theme.radius)
                                            color: Ui.field
                                            border.color: Ui.line
                                            Label {
                                                id: cap
                                                anchors.centerIn: parent
                                                text: parent.modelData
                                                font.family: Ui.mono
                                                font.pixelSize: Ui.tiny
                                                color: Ui.text
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
