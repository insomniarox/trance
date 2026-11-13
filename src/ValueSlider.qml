import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A thin slider that takes no more room than the line it sits in.
Slider {
    id: slider
    focusPolicy: Qt.NoFocus
    implicitWidth: 140
    implicitHeight: 24
    padding: 0
    hoverEnabled: true

    background: Rectangle {
        x: slider.leftPadding
        y: slider.topPadding + (slider.availableHeight - height) / 2
        width: slider.availableWidth
        height: 3
        radius: Math.min(1.5, theme.radius)
        color: "#2cffffff"
        Rectangle {
            width: slider.visualPosition * parent.width
            height: parent.height
            radius: parent.radius
            color: theme.accent
        }
    }
    handle: Rectangle {
        x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
        y: slider.topPadding + (slider.availableHeight - height) / 2
        width: 12
        height: 12
        radius: 6
        color: slider.pressed ? "white" : slider.hovered ? "#f4ffffff" : "#d8ffffff"
        scale: slider.pressed ? 1.15 : 1
        Behavior on scale { NumberAnimation { duration: 90 } }
    }
}
