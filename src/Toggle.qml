import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// An on/off switch, small enough for a line of settings.
Switch {
    id: toggle
    focusPolicy: Qt.NoFocus
    padding: 0
    implicitWidth: 36
    implicitHeight: 20
    hoverEnabled: true
    background: null
    contentItem: Item {}
    indicator: Rectangle {
        width: 36
        height: 20
        radius: 10
        color: toggle.checked ? theme.accent : toggle.hovered ? "#40ffffff" : "#30ffffff"
        Behavior on color { ColorAnimation { duration: 120 } }
        Rectangle {
            y: 3
            x: toggle.checked ? parent.width - width - 3 : 3
            width: 14
            height: 14
            radius: 7
            color: toggle.checked ? theme.accentForeground : "#e8ffffff"
            Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
        }
    }
}
