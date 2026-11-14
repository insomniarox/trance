import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// What is running right now, for when something looks or sounds wrong: the
// preset, how fast it is being drawn and at what size, and what is arriving
// from the music. Off unless asked for (D, or the settings).
Rectangle {
    id: overlay
    // The Visualizer whose drawing is being reported.
    property var view
    // The backend's side of it, asked for a few times a second.
    property var stats: backend.stats()

    width: Math.min(grid.implicitWidth + 28, parent.width - 32)
    height: grid.implicitHeight + 24
    radius: Math.min(4, theme.radius)
    color: "#d00a0a0e"
    border.color: Ui.line

    Timer {
        interval: 250
        repeat: true
        running: overlay.visible
        triggeredOnStart: true
        onTriggered: overlay.stats = backend.stats()
    }

    function number(value, places) { return Number(value).toFixed(places) }
    readonly property string presetLine: {
        const parts = [backend.currentName]
        parts.push(backend.currentIsYours ? qsTr("yours") : qsTr("built-in"))
        parts.push(Ui.count(stats.layers, qsTr("1 layer"), qsTr("%1 layers")))
        if (backend.dirty)
            parts.push(qsTr("Unsaved changes"))
        return parts.join("  ·  ")
    }
    readonly property string nextLine: {
        const waiting = stats.waiting
        if (waiting === "never")
            return qsTr("Never")
        if (waiting === "held")
            return qsTr("Held")
        if (waiting === "editing")
            return qsTr("Waiting while the editor is open")
        if (waiting === "unsaved")
            return qsTr("Waiting for unsaved changes")
        const left = Math.ceil(stats.nextIn)
        const when = left > 0 ? qsTr("In %1 s").arg(left) : qsTr("On the next beat")
        return when + "  ·  " + (backend.shuffle ? qsTr("Shuffle") : qsTr("In order"))
    }
    readonly property string drawingLine: view
        ? qsTr("%1 frames a second").arg(Math.round(view.fps)) + "  ·  "
          + qsTr("%1 ms a frame, %2 ms at worst").arg(number(view.frameMs, 1)).arg(number(view.worstMs, 1))
        : ""
    readonly property string sizeLine: {
        if (!view)
            return ""
        const drawn = view.pictureWidth + " × " + view.pictureHeight
        const shown = view.pixelWidth + " × " + view.pixelHeight
        return drawn === shown ? drawn : qsTr("%1, shown at %2").arg(drawn).arg(shown)
    }
    readonly property string soundLine: {
        const parts = [stats.source, number(stats.sampleRate / 1000, 0) + " kHz"]
        parts.push(backend.audioError.length > 0 ? backend.audioError : backend.silent ? qsTr("Silent") : qsTr("Playing"))
        return parts.join("  ·  ")
    }
    readonly property string colorLine: backend.activePalette.length === 0 ? qsTr("Each preset's own")
        : backend.colorDrift ? qsTr("%1, drifting").arg(backend.activePalette) : backend.activePalette
    readonly property string sceneLine: {
        const parts = [view && view.blending ? qsTr("Changing presets") : qsTr("One preset")]
        parts.push(Ui.count(view ? view.visitors : 0, qsTr("1 visitor"), qsTr("%1 visitors")))
        return parts.join("  ·  ")
    }

    component Name: Label {
        font.pixelSize: Ui.tiny
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.8
        color: Ui.textFaint
        Layout.alignment: Qt.AlignBaseline
    }
    component Value: Label {
        font.family: Ui.mono
        font.pixelSize: Ui.small
        color: Ui.text
        elide: Text.ElideRight
        Layout.fillWidth: true
        Layout.maximumWidth: 460
        Layout.alignment: Qt.AlignBaseline
    }

    GridLayout {
        id: grid
        x: 14
        y: 12
        width: Math.min(implicitWidth, overlay.parent.width - 60)
        columns: 2
        columnSpacing: 16
        rowSpacing: 5

        Name { text: qsTr("Preset") }
        Value { text: overlay.presetLine }
        Name { text: qsTr("Next") }
        Value { text: overlay.nextLine }
        Name { text: qsTr("Drawing") }
        Value { text: overlay.drawingLine }
        Name { text: qsTr("Size") }
        Value { text: overlay.sizeLine }
        Name { text: qsTr("Graphics") }
        Value { text: overlay.view ? overlay.view.graphics : "" }
        Name { text: qsTr("Sound") }
        Value { text: overlay.soundLine }
        Name { text: qsTr("Levels") }
        // By the names formulas use, so a number here can be typed there.
        Row {
            spacing: 14
            Layout.alignment: Qt.AlignVCenter
            Repeater {
                model: ["bass", "mid", "treb", "vol", "beat"]
                Row {
                    id: level
                    required property string modelData
                    readonly property real value: overlay.stats[modelData] || 0
                    spacing: 6
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: level.modelData
                        font.family: Ui.mono
                        font.pixelSize: Ui.small
                        color: Ui.textDim
                    }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 34
                        height: 4
                        color: "#24ffffff"
                        Rectangle {
                            height: parent.height
                            // Levels sit around 1 and peak near 2; the beat runs 0 to 1.
                            width: parent.width * Math.max(0, Math.min(1, level.value / (level.modelData === "beat" ? 1 : 2)))
                            color: level.modelData === "beat" ? "white" : theme.accent
                        }
                    }
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 30
                        text: level.value.toFixed(2)
                        font.family: Ui.mono
                        font.pixelSize: Ui.small
                        color: Ui.text
                    }
                }
            }
        }
        // The six narrow ranges, lowest first.
        Name { text: qsTr("Bands") }
        Row {
            spacing: 14
            Layout.alignment: Qt.AlignVCenter
            Repeater {
                model: overlay.stats.bands
                Row {
                    id: band
                    required property var modelData
                    spacing: 6
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: band.modelData.name
                        font.family: Ui.mono
                        font.pixelSize: Ui.small
                        color: Ui.textDim
                    }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 22
                        height: 4
                        color: "#24ffffff"
                        Rectangle {
                            height: parent.height
                            width: parent.width * Math.max(0, Math.min(1, band.modelData.level / 2))
                            color: theme.accent
                        }
                    }
                }
            }
        }
        Name { text: qsTr("Colors") }
        Value { text: overlay.colorLine }
        Name { text: qsTr("Scene") }
        Value { text: overlay.sceneLine }
        Name { text: qsTr("Problems"); visible: backend.problems.length > 0; color: Ui.danger }
        Value {
            visible: backend.problems.length > 0
            color: Ui.danger
            text: backend.problems.length > 0 ? backend.problems[0].message : ""
        }
    }
}
