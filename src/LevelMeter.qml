import QtQuick

// Bass, mid, treble, and the beat as the presets hear them: what a formula
// has to work with, and proof that the music is arriving.
Row {
    id: meter
    property real barHeight: 26
    spacing: 3
    Repeater {
        model: 4
        Rectangle {
            id: bar
            required property int index
            width: 5
            height: meter.barHeight
            radius: Math.min(2, theme.radius)
            color: "#1cffffff"
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                radius: parent.radius
                height: parent.height * Math.max(0, Math.min(1, backend.levels[bar.index] || 0))
                color: bar.index === 3 ? "white" : theme.accent
            }
        }
    }
}
