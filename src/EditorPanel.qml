import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The editor for the preset that is showing. Everything here belongs to
// that one preset and is saved with it; the app's own settings have a panel
// of their own. Changes show in the picture as they are made, and nothing
// is written to disk until the preset is saved.
SidePanel {
    id: panel
    title: qsTr("Preset")
    scope: backend.dirty ? qsTr("Unsaved changes")
         : backend.currentIsYours ? qsTr("Yours, saved") : qsTr("Built-in. Changes are saved as your own copy.")

    // Which layer cards are folded open, by position.
    property var open: ({})
    readonly property var schema: backend.schema
    property int knownLayers: 0
    Component.onCompleted: knownLayers = backend.layers.length

    readonly property var tabNames: [qsTr("Motion"), qsTr("Layers"), qsTr("Look"), "JSON"]
    // Which tabs hold something that differs from the saved preset.
    readonly property var tabEdited: {
        backend.revision
        const any = function (paths) { return paths.some(function (path) { return backend.isEdited(path) }) }
        return [any(["feedback", "pixel", "frame", "init"]), any(["layers"]),
                any(["palette", "background", "post", "shade"]), false]
    }
    // Which tab shows. The window keeps it, so it is asked to change it.
    property int tab: 0
    signal tabPicked(int index)

    component SectionTitle: Label {
        Layout.fillWidth: true
        Layout.topMargin: 14
        font.pixelSize: Ui.tiny
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.8
        color: Ui.textDim
    }
    component Note: Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        font.pixelSize: Ui.small
        color: Ui.textFaint
    }
    component Page: Flickable {
        default property alias content: pageColumn.data
        clip: true
        contentHeight: pageColumn.implicitHeight + Ui.pad
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        ColumnLayout {
            id: pageColumn
            x: Ui.pad
            width: parent.width - 2 * Ui.pad
            spacing: 6
        }
    }

    tools: [
        IconButton { glyph: "undo"; tip: qsTr("Undo"); enabled: backend.canUndo; onClicked: backend.undo() },
        IconButton { glyph: "redo"; tip: qsTr("Redo"); enabled: backend.canRedo; onClicked: backend.redo() }
    ]

    Connections {
        target: backend
        // A layer that was just added opens, ready to be changed.
        function onLayersChanged() {
            if (backend.layers.length > panel.knownLayers) {
                const next = {}
                next[backend.layers.length - 1] = true
                panel.open = next
            }
            panel.knownLayers = backend.layers.length
        }
        function onCurrentChanged() {
            panel.open = ({})
            panel.knownLayers = backend.layers.length
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // The preset's name, and keeping it.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: Ui.pad
            Layout.bottomMargin: 10
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                CompactField {
                    id: nameBox
                    Layout.fillWidth: true
                    mono: false
                    placeholder: qsTr("Name")
                    textSize: 14
                    textWeight: Font.DemiBold
                    implicitHeight: 34
                    readonly property var current: { backend.revision; return backend.param("name") }
                    onCurrentChanged: if (!editing) text = current
                    Component.onCompleted: text = current
                    onCommitted: function (text) { backend.setParam("name", text) }
                    onCancelled: text = current
                }
                LevelMeter { barHeight: 30 }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                TextButton { text: qsTr("Save"); primary: true; enabled: backend.dirty; onClicked: backend.save() }
                TextButton { text: qsTr("Save as"); onClicked: win.saveAs() }
                Item { Layout.fillWidth: true }
                TextButton { text: qsTr("Revert"); enabled: backend.dirty; onClicked: backend.revert() }
            }
        }

        // Motion, layers, look, or the whole preset as text.
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Ui.line }
            Row {
                x: Ui.pad - 10
                height: parent.height
                Repeater {
                    model: panel.tabNames
                    AbstractButton {
                        id: tabButton
                        required property string modelData
                        required property int index
                        readonly property bool current: panel.tab === index
                        height: parent.height
                        width: tabLabel.implicitWidth + 20
                        focusPolicy: Qt.NoFocus
                        hoverEnabled: true
                        onClicked: panel.tabPicked(index)
                        contentItem: Item {
                            Label {
                                id: tabLabel
                                anchors.centerIn: parent
                                text: tabButton.modelData
                                font.pixelSize: Ui.body
                                font.weight: tabButton.current ? Font.DemiBold : Font.Normal
                                color: tabButton.current ? Ui.text : tabButton.hovered ? Ui.text : Ui.textDim
                            }
                            // Something on this tab has been changed since the save.
                            Rectangle {
                                visible: panel.tabEdited[tabButton.index]
                                anchors { left: tabLabel.right; leftMargin: 3; top: tabLabel.top }
                                width: 5
                                height: 5
                                radius: 2.5
                                color: theme.accent
                            }
                            Rectangle {
                                anchors { bottom: parent.bottom; left: parent.left; right: parent.right; leftMargin: 10; rightMargin: 10 }
                                height: 2
                                color: theme.accent
                                visible: tabButton.current
                            }
                        }
                    }
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: panel.tab

            // How last frame's picture is bent before this one is drawn on it.
            Page {
                SectionTitle { text: qsTr("Feedback") }
                Note { text: qsTr("Each frame, the last one is zoomed, turned and faded, then drawn over. Type a formula in any box to make it follow the music. Rest the pointer on a name to see what it does; a name in color has been changed since the save.") }
                Item { Layout.preferredHeight: 2 }
                Repeater {
                    model: panel.schema.feedback
                    ParamRow {
                        required property var modelData
                        Layout.fillWidth: true
                        path: "feedback." + modelData.key
                        field: modelData
                    }
                }
                SectionTitle { text: qsTr("Code") }
                CodeField {
                    Layout.fillWidth: true
                    path: "pixel"
                    label: "pixel"
                    help: panel.schema.code.pixel.help
                    hint: qsTr("per pixel: x, y, rad, ang; change zoom, rot, dx…")
                }
                CodeField {
                    Layout.fillWidth: true
                    path: "frame"
                    label: "frame"
                    help: panel.schema.code.frame.help
                    hint: qsTr("once a frame; make your own variables")
                }
                CodeField {
                    Layout.fillWidth: true
                    path: "init"
                    label: "init"
                    help: panel.schema.code.init.help
                    hint: qsTr("once, when the preset starts")
                }
                SectionTitle { text: qsTr("Reference") }
                Note { text: qsTr("Variables") + ": " + panel.schema.variables.join("  ") }
                Note { text: qsTr("Functions") + ": " + panel.schema.functions.join("  ") }
                Note { text: qsTr("Colors") + ": #rrggbb  palette(t)  hsv(h, s, v)  rgb(r, g, b)  " + panel.schema.themeColors.slice(0, 13).join("  ") }
            }

            // What is drawn on top each frame.
            Page {
                SectionTitle { text: qsTr("Add a layer") }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: panel.schema.layerTypes
                        TextButton {
                            id: addButton
                            required property string modelData
                            readonly property var kind: panel.schema.types[modelData]
                            text: kind.label
                            leftPadding: 22
                            onHoveredChanged: hovered ? win.showHelp(addButton, kind.label, kind.help, "") : win.hideHelp(addButton)
                            implicitWidth: implicitContentWidth + 40
                            onClicked: backend.addLayer(modelData)
                            Glyph { x: 9; anchors.verticalCenter: parent.verticalCenter; name: "plus"; size: 12; color: Ui.textDim }
                        }
                    }
                }
                SectionTitle { text: qsTr("Layers") }
                Note {
                    visible: backend.layers.length === 0
                    text: qsTr("Nothing is drawn yet. Add a layer above.")
                }
                Repeater {
                    model: backend.layers
                    LayerCard {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        layerIndex: index
                        type: modelData.type
                        total: backend.layers.length
                        expanded: panel.open[index] === true
                        onToggled: {
                            const next = Object.assign({}, panel.open)
                            next[index] = !expanded
                            panel.open = next
                        }
                    }
                }
            }

            // Colors, and what happens on the way to the screen.
            Page {
                SectionTitle { text: qsTr("Palette") }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Picker {
                        id: paletteBox
                        Layout.preferredWidth: 130
                        model: panel.schema.palettes
                        readonly property var current: { backend.revision; return backend.param("palette") }
                        onCurrentChanged: currentIndex = panel.schema.palettes.indexOf(current)
                        Component.onCompleted: currentIndex = panel.schema.palettes.indexOf(current)
                        displayText: currentIndex < 0 ? qsTr("Custom") : currentText
                        onActivated: backend.setParam("palette", currentText)
                    }
                    CompactField {
                        Layout.fillWidth: true
                        readonly property var current: { backend.revision; return backend.param("palette") }
                        readonly property string problem: { backend.problems; return backend.problemAt("palette") }
                        invalid: problem.length > 0
                        onCurrentChanged: if (!editing) text = current
                        Component.onCompleted: text = current
                        onCommitted: function (text) { backend.setParam("palette", text) }
                        onCancelled: text = current
                    }
                }
                Row {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 10
                    Repeater {
                        model: backend.paletteColors
                        Rectangle {
                            required property string modelData
                            width: parent.width / Math.max(1, backend.paletteColors.length)
                            height: 10
                            color: modelData
                        }
                    }
                }
                Note { text: qsTr("A name from the list, or your own colors separated by commas. Theme names such as accent follow your desktop.") }
                // The app's color setting wins over a preset's palette, which
                // would otherwise look like the palette here doing nothing.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: 2
                    visible: backend.activePalette.length > 0
                    implicitHeight: overridden.implicitHeight + 16
                    radius: Math.min(4, theme.radius)
                    color: Ui.card
                    Label {
                        id: overridden
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 10 }
                        text: qsTr("Showing %1 instead: the Colors setting applies to every preset.").arg(backend.activePalette)
                        wrapMode: Text.WordWrap
                        font.pixelSize: Ui.small
                        color: Ui.warning
                    }
                }

                SectionTitle { text: qsTr("Background") }
                Note { text: qsTr("Shows where the picture is dark. It is drawn fresh every frame, so it never smears.") }
                Item { Layout.preferredHeight: 2 }
                Repeater {
                    model: panel.schema.background
                    ParamRow {
                        required property var modelData
                        Layout.fillWidth: true
                        path: "background." + modelData.key
                        field: modelData
                    }
                }

                SectionTitle { text: qsTr("Finish") }
                Repeater {
                    model: panel.schema.post
                    ParamRow {
                        required property var modelData
                        Layout.fillWidth: true
                        path: "post." + modelData.key
                        field: modelData
                    }
                }
                SectionTitle { text: qsTr("Code") }
                CodeField {
                    Layout.fillWidth: true
                    path: "shade"
                    label: "shade"
                    help: panel.schema.code.shade.help
                    hint: qsTr("per pixel: change r, g, b; read x, y, rad, ang, lum")
                }
            }

            // The whole preset as text, for everything at once.
            ColumnLayout {
                spacing: 6
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: Ui.pad
                    Layout.bottomMargin: jsonProblem.visible ? 0 : Ui.pad
                    radius: Math.min(4, theme.radius)
                    color: jsonEdit.activeFocus ? Ui.fieldHover : Ui.field
                    border.width: 1
                    border.color: jsonProblem.visible ? Ui.danger : jsonEdit.activeFocus ? theme.accent : "transparent"
                    Flickable {
                        id: jsonView
                        anchors.fill: parent
                        anchors.margins: 10
                        clip: true
                        contentWidth: width
                        contentHeight: jsonEdit.implicitHeight
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        TextEdit {
                            id: jsonEdit
                            width: jsonView.width - 12
                            color: Ui.text
                            selectionColor: theme.accent
                            selectedTextColor: theme.accentForeground
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            font.family: Ui.mono
                            font.pixelSize: Ui.small
                            readonly property string current: backend.presetText
                            onCurrentChanged: if (!activeFocus) text = current
                            Component.onCompleted: text = current
                            onTextChanged: if (activeFocus) jsonApply.restart()
                            onActiveFocusChanged: if (!activeFocus) text = current
                            // Keep the cursor in view while typing.
                            onCursorRectangleChanged: {
                                if (cursorRectangle.y < jsonView.contentY)
                                    jsonView.contentY = cursorRectangle.y
                                else if (cursorRectangle.y + cursorRectangle.height > jsonView.contentY + jsonView.height)
                                    jsonView.contentY = cursorRectangle.y + cursorRectangle.height - jsonView.height
                            }
                            Timer { id: jsonApply; interval: 700; onTriggered: backend.setPresetText(jsonEdit.text) }
                        }
                    }
                }
                Label {
                    id: jsonProblem
                    readonly property string problem: { backend.problems; return backend.problemAt("json") }
                    visible: problem.length > 0
                    text: problem
                    color: Ui.danger
                    wrapMode: Text.WordWrap
                    font.pixelSize: Ui.small
                    Layout.fillWidth: true
                    Layout.margins: Ui.pad
                    Layout.topMargin: 0
                }
            }
        }

        // Everything wrong with the preset as it stands.
        Rectangle {
            Layout.fillWidth: true
            visible: backend.problems.length > 0
            implicitHeight: problemList.implicitHeight + 20
            color: "#14ff8a8a"
            Rectangle { width: parent.width; height: 1; color: Ui.line }
            ColumnLayout {
                id: problemList
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: Ui.pad }
                spacing: 3
                Repeater {
                    model: backend.problems.slice(0, 4)
                    Label {
                        required property var modelData
                        Layout.fillWidth: true
                        text: (modelData.path.length > 0 ? modelData.path + ": " : "") + modelData.message
                        color: modelData.error ? Ui.danger : Ui.warning
                        wrapMode: Text.WordWrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        font.pixelSize: Ui.small
                    }
                }
            }
        }
    }
}
