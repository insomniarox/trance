import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A button that is only an icon, with what it does as a tooltip.
AbstractButton {
    id: button
    property string glyph
    property string tip
    property real glyphSize: 16
    property color tint: Ui.text
    implicitWidth: Ui.control
    implicitHeight: Ui.control
    focusPolicy: Qt.NoFocus
    hoverEnabled: true

    background: Rectangle {
        radius: Math.min(4, theme.radius)
        color: button.down ? Ui.fieldActive : button.hovered && button.enabled ? Ui.hover : "transparent"
    }
    contentItem: Item {
        Glyph {
            anchors.centerIn: parent
            name: button.glyph
            size: button.glyphSize
            color: button.tint
            opacity: !button.enabled ? 0.28 : button.hovered ? 1 : 0.78
        }
    }

    ToolTip.visible: hovered && tip.length > 0
    ToolTip.delay: 600
    ToolTip.text: tip
}
