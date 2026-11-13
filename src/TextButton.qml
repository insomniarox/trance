import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A button with a word on it. The one that carries the panel's main action
// is `primary` and takes the accent.
AbstractButton {
    id: button
    property bool primary: false
    implicitWidth: Math.max(64, label.implicitWidth + 28)
    implicitHeight: Ui.control
    focusPolicy: Qt.NoFocus
    hoverEnabled: true
    font.pixelSize: Ui.body

    readonly property bool lit: primary && enabled

    background: Rectangle {
        radius: Math.min(4, theme.radius)
        color: button.lit ? theme.accent
             : button.down ? Ui.fieldActive : button.hovered && button.enabled ? Ui.fieldHover : Ui.field
        opacity: button.lit && button.down ? 0.8 : 1
    }
    contentItem: Label {
        id: label
        text: button.text
        font.pixelSize: button.font.pixelSize
        font.weight: button.lit ? Font.DemiBold : Font.Normal
        color: button.lit ? theme.accentForeground : Ui.text
        opacity: button.enabled ? 1 : 0.35
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
