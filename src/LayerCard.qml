import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One layer of a preset: fold it open to change its settings, or use the
// buttons to reorder, copy, and remove it.
Rectangle {
    id: card
    property int layerIndex
    property string type
    property bool expanded: false
    property int total: 1
    signal toggled()

    readonly property string base: "layers." + layerIndex + "."
    readonly property var fields: backend.schema.layers[type] || []
    readonly property var enabledValue: { backend.revision; return backend.param(base + "enabled") }
    readonly property bool on: enabledValue !== false && enabledValue !== 0
    readonly property var kind: backend.schema.types[type] || ({ label: type, help: "" })
    // Anything in this layer differs from the saved preset.
    readonly property bool edited: { backend.revision; return backend.isEdited("layers." + layerIndex) }

    implicitHeight: column.implicitHeight
    radius: Math.min(4, theme.radius)
    color: Ui.card

    ColumnLayout {
        id: column
        anchors { left: parent.left; right: parent.right; top: parent.top }
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            Layout.leftMargin: 8
            Layout.rightMargin: 6
            spacing: 2
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Glyph {
                    id: fold
                    anchors.verticalCenter: parent.verticalCenter
                    name: "chevron-right"
                    size: 14
                    color: Ui.textDim
                    rotation: card.expanded ? 90 : 0
                    Behavior on rotation { NumberAnimation { duration: 120 } }
                }
                Label {
                    id: title
                    anchors { left: fold.right; leftMargin: 8; verticalCenter: parent.verticalCenter }
                    width: Math.min(implicitWidth, parent.width - fold.width - 26)
                    text: card.kind.label
                    font.weight: Font.DemiBold
                    font.pixelSize: Ui.body
                    color: card.edited ? theme.accent : Ui.text
                    opacity: card.on ? 1 : 0.5
                    elide: Text.ElideRight
                }
                // The mark of a layer that has been changed since the save.
                Rectangle {
                    visible: card.edited
                    anchors { left: title.right; leftMargin: 7; verticalCenter: parent.verticalCenter }
                    width: 6
                    height: 6
                    radius: 3
                    color: theme.accent
                }
                TapHandler { onTapped: card.toggled() }
                HoverHandler {
                    onHoveredChanged: hovered ? win.showHelp(title, card.kind.label, card.kind.help, "") : win.hideHelp(title)
                }
            }
            IconButton { glyph: "up"; tip: qsTr("Move up"); glyphSize: 14; enabled: card.layerIndex > 0; onClicked: backend.moveLayer(card.layerIndex, -1) }
            IconButton { glyph: "down"; tip: qsTr("Move down"); glyphSize: 14; enabled: card.layerIndex < card.total - 1; onClicked: backend.moveLayer(card.layerIndex, 1) }
            IconButton { glyph: "copy"; tip: qsTr("Duplicate"); glyphSize: 14; onClicked: backend.duplicateLayer(card.layerIndex) }
            IconButton { glyph: "trash"; tip: qsTr("Remove"); glyphSize: 14; onClicked: backend.removeLayer(card.layerIndex) }
            Toggle {
                Layout.leftMargin: 6
                Layout.rightMargin: 4
                checked: card.on
                onToggled: backend.setParam(card.base + "enabled", checked)
            }
        }

        Loader {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.bottomMargin: active ? 12 : 0
            active: card.expanded
            visible: active
            sourceComponent: ColumnLayout {
                spacing: 6
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.bottomMargin: 4; color: Ui.line }
                Repeater {
                    model: card.fields.filter(function (f) { return f.type !== "code" && f.key !== "enabled" })
                    ParamRow {
                        required property var modelData
                        Layout.fillWidth: true
                        path: card.base + modelData.key
                        field: modelData
                    }
                }
                Repeater {
                    model: card.fields.filter(function (f) { return f.type === "code" })
                    CodeField {
                        required property var modelData
                        Layout.fillWidth: true
                        path: card.base + modelData.key
                        label: modelData.key
                        help: modelData.help
                        hint: card.type === "shape" ? qsTr("runs for every copy: n, count, x, y, radius, angle, r g b a")
                                                    : qsTr("runs for every point: t, i, v, x, y, r g b a")
                    }
                }
            }
        }
    }
}
