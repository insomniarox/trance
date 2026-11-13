import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One setting of a preset: its name, and a slider, switch, list, or color
// box to change it. Any number can be typed as a formula instead, which is
// how a setting comes to follow the music.
ColumnLayout {
    id: row
    property string path
    property var field
    spacing: 2

    readonly property var current: { backend.revision; return backend.param(row.path) }
    readonly property string problem: { backend.problems; return backend.problemAt(row.path) }
    readonly property bool numeric: field.type === "number" || field.type === "integer" || field.type === "toggle"
    readonly property bool formula: numeric && typeof current === "string"
    // A setting the preset says nothing about runs on its default, and
    // shows fainter for it.
    readonly property bool changed: { backend.revision; return backend.isSet(row.path) }
    // One that no longer matches the saved preset is marked until it is
    // saved or put back.
    readonly property bool edited: { backend.revision; return backend.isEdited(row.path) }
    // The setting's range and default, and its name in the file.
    readonly property string detail: {
        const parts = [row.path.replace(/^layers\.\d+\./, "")]
        if (numeric && field.type !== "toggle")
            parts.push(qsTr("%1 to %2").arg(field.min).arg(field.max))
        if (field.type === "choice" || field.type === "color")
            parts.push(qsTr("default %1").arg(field.text))
        else if (field.type === "toggle")
            parts.push(qsTr("default %1").arg(field.def ? qsTr("on") : qsTr("off")))
        else if (field.type !== "code")
            parts.push(qsTr("default %1").arg(field.def))
        return parts.join("  ·  ")
    }

    function display(value) {
        if (typeof value === "number")
            return String(Number(value.toFixed(5)))
        return value === undefined || value === null ? "" : String(value)
    }
    function rounded(value) {
        if (field.type === "integer")
            return Math.round(value)
        const places = (field.max - field.min) < 0.5 ? 4 : 3
        return Number(value.toFixed(places))
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: Ui.control
        spacing: 10
        Label {
            id: name
            text: row.field.label
            Layout.preferredWidth: 118
            elide: Text.ElideRight
            font.pixelSize: Ui.body
            font.weight: row.edited ? Font.DemiBold : Font.Normal
            color: row.edited ? theme.accent : row.changed ? Ui.text : Ui.textDim
            // The mark of a setting that has been changed since the save.
            Rectangle {
                visible: row.edited
                x: -9
                width: 3
                height: parent.height
                radius: Math.min(1.5, theme.radius)
                color: theme.accent
            }
            // Double-click a name to put the setting back to its default.
            TapHandler { onDoubleTapped: backend.resetParam(row.path) }
            HoverHandler {
                onHoveredChanged: hovered ? win.showHelp(name, row.field.label, row.field.help, row.detail)
                                          : win.hideHelp(name)
            }
        }
        Loader {
            Layout.fillWidth: true
            sourceComponent: row.field.type === "choice" ? choiceControl
                           : row.field.type === "color" ? colorControl
                           : row.field.type === "toggle" && !row.formula ? toggleControl : numberControl
        }
    }
    Label {
        visible: row.problem.length > 0
        text: row.problem
        color: Ui.danger
        wrapMode: Text.WordWrap
        font.pixelSize: Ui.small
        Layout.fillWidth: true
        Layout.leftMargin: 128
    }

    Component {
        id: numberControl
        RowLayout {
            spacing: 10
            function sync() {
                if (!slider.pressed)
                    slider.value = row.formula ? row.field.def : Number(row.current)
                if (!box.editing)
                    box.text = row.display(row.current)
            }
            Component.onCompleted: sync()
            Connections { target: row; function onCurrentChanged() { sync() } }
            ValueSlider {
                id: slider
                visible: !row.formula && row.field.type !== "toggle"
                Layout.fillWidth: true
                from: row.field.min
                to: row.field.max
                stepSize: row.field.type === "integer" ? 1 : 0
                onMoved: backend.setParam(row.path, row.rounded(value))
            }
            CompactField {
                id: box
                Layout.fillWidth: row.formula || row.field.type === "toggle"
                Layout.preferredWidth: 78
                invalid: row.problem.length > 0
                onCommitted: function (text) { backend.setParam(row.path, text) }
                onCancelled: text = row.display(row.current)
            }
        }
    }

    Component {
        id: toggleControl
        RowLayout {
            Toggle {
                checked: row.current === true || row.current === 1
                onToggled: backend.setParam(row.path, checked)
            }
            Item { Layout.fillWidth: true }
        }
    }

    Component {
        id: choiceControl
        Picker {
            model: row.field.options
            function sync() { currentIndex = row.field.options.indexOf(row.current) }
            Component.onCompleted: sync()
            Connections { target: row; function onCurrentChanged() { sync() } }
            onActivated: backend.setParam(row.path, currentText)
        }
    }

    Component {
        id: colorControl
        RowLayout {
            spacing: 8
            function sync() {
                if (!box.editing)
                    box.text = row.display(row.current)
            }
            Component.onCompleted: sync()
            Connections { target: row; function onCurrentChanged() { sync() } }
            // A fixed color shows as itself; one that moves with the music
            // or the palette shows the palette it draws from.
            Rectangle {
                id: swatch
                readonly property string fixed: { backend.revision; return backend.colorPreview(row.display(row.current)) }
                readonly property var stops: backend.paletteColors
                Layout.preferredWidth: Ui.control
                Layout.preferredHeight: Ui.control
                radius: Math.min(4, theme.radius)
                border.color: Ui.line
                border.width: 1
                color: fixed.length > 0 ? fixed : "transparent"
                gradient: fixed.length > 0 || stops.length < 2 ? null : dynamicGradient
                Gradient {
                    id: dynamicGradient
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: swatch.stops.length > 0 ? swatch.stops[0] : "black" }
                    GradientStop { position: 0.5; color: swatch.stops.length > 1 ? swatch.stops[Math.floor(swatch.stops.length / 2)] : "black" }
                    GradientStop { position: 1; color: swatch.stops.length > 1 ? swatch.stops[swatch.stops.length - 1] : "black" }
                }
            }
            CompactField {
                id: box
                Layout.fillWidth: true
                invalid: row.problem.length > 0
                onCommitted: function (text) { backend.setParam(row.path, text) }
                onCancelled: text = row.display(row.current)
            }
        }
    }
}
