import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// What this copy of Trance is, shown with A: its version and how it was
// built, the system it runs on, what is in the library, and where your
// presets and settings are kept.
Rectangle {
    id: overlay
    color: Ui.scrim
    // The picture, for the graphics card it is drawn on.
    property var view: null
    signal dismissed()
    signal copied()

    readonly property var about: backend.about
    readonly property string graphics: view ? view.graphics : ""
    readonly property string glVersion: view && view.details ? (view.details.glVersion || "") : ""
    readonly property string qtLine: about.qt === about.qtBuilt ? about.qt
                                     : qsTr("%1, built with %2").arg(about.qt).arg(about.qtBuilt)

    readonly property var groups: [
        { title: qsTr("This copy"), rows: [
            [qsTr("Qt"), qtLine],
            [qsTr("Compiler"), about.compiler],
            [qsTr("Sound"), about.sound],
            [qsTr("Track titles"), about.titles],
            [qsTr("Colors"), theme.fromDesktop ? qsTr("The desktop theme's") : qsTr("The built-in set")]
        ] },
        { title: qsTr("This computer"), rows: [
            [qsTr("System"), about.system],
            [qsTr("Kernel"), about.kernel],
            [qsTr("Processor"), about.architecture],
            [qsTr("Display"), about.display],
            [qsTr("Graphics"), graphics],
            [qsTr("OpenGL"), glVersion]
        ] }
    ]
    readonly property var library: [
        [about.builtinPresets, qsTr("built-in presets")],
        [about.yourPresets, qsTr("of your own")],
        [about.palettes, qsTr("palettes")],
        [about.visitors, qsTr("visitors")]
    ]

    // The same, as text for a bug report. The folders are left out: they
    // carry the user's name.
    function details() {
        let lines = ["Trance " + about.version]
        for (const group of groups)
            for (const row of group.rows)
                if (row[1].length > 0)
                    lines.push(row[0] + ": " + row[1])
        lines.push(qsTr("Presets: %1 built-in, %2 of yours; %3 palettes; %4 visitors")
                   .arg(about.builtinPresets).arg(about.yourPresets).arg(about.palettes).arg(about.visitors))
        return lines.join("\n")
    }

    MouseArea { anchors.fill: parent; onClicked: overlay.dismissed() }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 720)
        height: Math.min(parent.height - 48, body.implicitHeight + heading.height + footer.implicitHeight + 92)
        radius: theme.radius
        color: Ui.popup
        border.color: Ui.line

        MouseArea { anchors.fill: parent }

        RowLayout {
            id: heading
            anchors { top: parent.top; left: parent.left; right: parent.right; topMargin: 22; leftMargin: 28; rightMargin: 16 }
            spacing: 14
            // The app's icon, in the desktop's accent.
            Rectangle {
                Layout.preferredWidth: 44
                Layout.preferredHeight: 44
                radius: Math.min(10, Math.max(4, theme.radius))
                color: theme.accent
                Row {
                    anchors.centerIn: parent
                    spacing: 2
                    Repeater {
                        model: [14, 32, 44, 26, 10]
                        Rectangle {
                            required property int modelData
                            anchors.verticalCenter: parent.verticalCenter
                            width: 4
                            height: modelData * 0.6
                            radius: 2
                            color: theme.accentForeground
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Label {
                    text: "Trance " + overlay.about.version
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    color: Ui.text
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("A music visualizer for Linux and Windows 11")
                    font.pixelSize: Ui.small
                    color: Ui.textDim
                    elide: Text.ElideRight
                }
            }
            IconButton {
                Layout.alignment: Qt.AlignTop
                glyph: "close"
                onClicked: overlay.dismissed()
            }
        }

        Flickable {
            id: scroller
            anchors { top: heading.bottom; left: parent.left; right: parent.right; bottom: footer.top; margins: 28; topMargin: 20; bottomMargin: 16 }
            contentHeight: body.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            ColumnLayout {
                id: body
                width: parent.width
                spacing: 22

                GridLayout {
                    Layout.fillWidth: true
                    columns: width > 560 ? 2 : 1
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
                            spacing: 6
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
                                model: group.modelData.rows
                                RowLayout {
                                    id: line
                                    required property var modelData
                                    Layout.fillWidth: true
                                    visible: modelData[1].length > 0
                                    spacing: 12
                                    Label {
                                        Layout.preferredWidth: 110
                                        Layout.alignment: Qt.AlignTop
                                        text: line.modelData[0]
                                        font.pixelSize: Ui.body
                                        color: Ui.textDim
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: line.modelData[1]
                                        font.pixelSize: Ui.body
                                        color: Ui.text
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }
                        }
                    }
                }

                // What is in the library, as four counts.
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        text: qsTr("Library")
                        font.pixelSize: Ui.tiny
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 0.8
                        color: Ui.textDim
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Repeater {
                            model: overlay.library
                            Rectangle {
                                id: count
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                implicitHeight: 58
                                radius: Math.min(4, theme.radius)
                                color: Ui.card
                                Column {
                                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 12; rightMargin: 12 }
                                    spacing: 1
                                    Label {
                                        text: count.modelData[0]
                                        font.pixelSize: 20
                                        font.weight: Font.DemiBold
                                        color: Ui.text
                                    }
                                    Label {
                                        width: parent.width
                                        text: count.modelData[1]
                                        font.pixelSize: Ui.tiny
                                        color: Ui.textDim
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                }

                // Where your things are, each with a way to open it.
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Label {
                        text: qsTr("Files")
                        font.pixelSize: Ui.tiny
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 0.8
                        color: Ui.textDim
                        Layout.bottomMargin: 2
                    }
                    Repeater {
                        model: [
                            [qsTr("Your presets"), overlay.about.presetsFolder, "presets"],
                            [qsTr("Settings"), overlay.about.settingsFile, "settings"]
                        ]
                        RowLayout {
                            id: file
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 12
                            Label {
                                Layout.preferredWidth: 110
                                text: file.modelData[0]
                                font.pixelSize: Ui.body
                                color: Ui.textDim
                            }
                            Label {
                                Layout.fillWidth: true
                                text: file.modelData[1]
                                font.family: Ui.mono
                                font.pixelSize: Ui.small
                                color: Ui.text
                                elide: Text.ElideMiddle
                            }
                            IconButton {
                                glyph: "folder"
                                tip: qsTr("Open the folder")
                                onClicked: backend.openFolder(file.modelData[2])
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            id: footer
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: 28; rightMargin: 28; bottomMargin: 22 }
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: qsTr("MIT License · Copyright (c) 2026 insomniarox")
                font.pixelSize: Ui.tiny
                color: Ui.textFaint
                elide: Text.ElideRight
            }
            TextButton {
                text: qsTr("Source code")
                onClicked: Qt.openUrlExternally("https://github.com/insomniarox/trance")
            }
            TextButton {
                text: qsTr("Report a problem")
                onClicked: Qt.openUrlExternally("https://github.com/insomniarox/trance/issues")
            }
            TextButton {
                primary: true
                text: qsTr("Copy details")
                onClicked: {
                    backend.copyText(overlay.details())
                    overlay.copied()
                }
            }
        }
    }
}
