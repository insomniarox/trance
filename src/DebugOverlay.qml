import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// What is running right now, for when something looks or sounds wrong: the
// preset and where it came from, how hard the drawing is working, what is
// arriving from the music, and the colors. Off unless asked for (D, or the
// settings). It is small and half see-through, on a thin dark backing that
// keeps it readable over light without hiding the picture under it.
Item {
    id: overlay
    // The Visualizer whose drawing is being reported.
    property var view
    // The backend's side of it, asked for a few times a second.
    property var stats: backend.stats()
    readonly property var details: view ? view.details : ({})

    readonly property int padX: 10
    readonly property int padY: 7
    width: Math.min(grid.implicitWidth + 2 * padX, parent.width - 32)
    height: grid.implicitHeight + 2 * padY
    opacity: 0.85

    Rectangle {
        anchors.fill: parent
        radius: Math.min(4, theme.radius)
        color: "#73000000"
    }

    Timer {
        interval: 250
        repeat: true
        running: overlay.visible
        triggeredOnStart: true
        onTriggered: overlay.stats = backend.stats()
    }

    readonly property int nameSize: 9
    readonly property int valueSize: 10
    readonly property color outline: "#70000000"

    function number(value, places) { return Number(value || 0).toFixed(places) }
    function thousands(value) { return Number(value || 0).toLocaleString(Qt.locale("en_US"), "f", 0) }
    function join(parts) { return parts.filter(function (p) { return p && String(p).length > 0 }).join("  ·  ") }

    readonly property string presetLine: join([
        backend.currentName,
        backend.currentShadowsBuiltin ? qsTr("your copy of a built-in") : backend.currentIsYours ? qsTr("yours") : qsTr("built-in"),
        backend.dirty ? qsTr("unsaved") : ""])
    readonly property string layerLine: stats.layerSummary && stats.layerSummary.length > 0 ? stats.layerSummary : qsTr("no layers")
    readonly property string problemLine: {
        const parts = []
        if (stats.errors > 0)
            parts.push(Ui.count(stats.errors, qsTr("1 error"), qsTr("%1 errors")))
        if (stats.warnings > 0)
            parts.push(Ui.count(stats.warnings, qsTr("1 warning"), qsTr("%1 warnings")))
        if (backend.problems.length > 0)
            parts.push(backend.problems[0].message)
        return join(parts)
    }
    readonly property string nextLine: {
        const waiting = stats.waiting
        if (waiting === "never") return qsTr("never")
        if (waiting === "held") return qsTr("held")
        if (waiting === "editing") return qsTr("waiting while the editor is open")
        if (waiting === "unsaved") return qsTr("waiting for unsaved changes")
        if (waiting === "alone") return qsTr("nothing to change to")
        const left = Math.ceil(stats.nextIn)
        return join([left > 0 ? qsTr("in %1 s").arg(left) : qsTr("on the next beat"),
                     backend.shuffle ? qsTr("shuffle") : qsTr("in order"),
                     backend.keepToFilter && backend.filtering && backend.matchCount > 0 ? qsTr("among %1 filtered").arg(backend.matchCount) : "",
                     qsTr("%1 s blend, %2").arg(number(backend.blendSeconds, 1)).arg(backend.transition)])
    }
    readonly property string changeLine: join([
        view && view.blending ? qsTr("%1 %2 %").arg(details.changePattern || "").arg(Math.round(100 * (details.changeProgress || 0))) : "",
        details.building ? qsTr("building shaders") : ""])
    readonly property string drawingLine: view
        ? join([qsTr("%1 fps").arg(Math.round(view.fps)),
                qsTr("%1 ms a frame").arg(number(view.frameMs, 1)),
                qsTr("%1 ms worst").arg(number(view.worstMs, 1))])
        : ""
    readonly property string engineLine: join([
        qsTr("%1 ms in the engine").arg(number(details.engineMs, 2)),
        qsTr("%1 vertices").arg(thousands(details.vertices)),
        details.particles === 1 ? qsTr("1 particle") : qsTr("%1 particles").arg(thousands(details.particles))])
    readonly property string sizeLine: {
        if (!view)
            return ""
        const drawn = view.pictureWidth + "×" + view.pictureHeight
        const shown = view.pixelWidth + "×" + view.pixelHeight
        return join([drawn === shown ? drawn : qsTr("%1 drawn, %2 shown").arg(drawn).arg(shown),
                     qsTr("quality %1 %").arg(Math.round(100 * backend.quality)),
                     backend.glass ? qsTr("glass %1 %").arg(Math.round(100 * backend.glassOpacity)) : ""])
    }
    readonly property string graphicsLine: view ? join([view.graphics, details.glVersion]) : ""
    readonly property string visitorLine: {
        const names = details.visitorNames || []
        return names.length > 0 ? names.join(", ") : backend.visitors ? qsTr("none right now") : qsTr("off")
    }
    readonly property string soundLine: join([
        stats.source, number(stats.sampleRate / 1000, 0) + " kHz",
        backend.audioError.length > 0 ? backend.audioError : backend.silent ? qsTr("silent") : qsTr("playing")])
    readonly property string tempoLine: join([
        stats.tempo > 0 ? qsTr("%1 BPM").arg(Math.round(stats.tempo)) : qsTr("no steady beat"),
        stats.beats === 1 ? qsTr("1 beat") : qsTr("%1 beats").arg(thousands(stats.beats)),
        qsTr("sensitivity ×%1").arg(number(stats.sensitivity, 2))])
    readonly property string trackLine: join([nowPlaying.title, nowPlaying.artist])
    readonly property string colorLine: join([
        backend.activePalette.length === 0 ? qsTr("each preset's own") : backend.activePalette,
        backend.colorDrift ? qsTr("drifting") : ""])

    component Name: Text {
        font.pixelSize: overlay.nameSize
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.6
        color: Ui.textFaint
        style: Text.Outline
        styleColor: overlay.outline
        Layout.alignment: Qt.AlignBaseline | Qt.AlignRight
    }
    component Value: Text {
        font.family: Ui.mono
        font.pixelSize: overlay.valueSize
        color: Ui.text
        style: Text.Outline
        styleColor: overlay.outline
        elide: Text.ElideRight
        Layout.fillWidth: true
        Layout.maximumWidth: 420
        Layout.alignment: Qt.AlignBaseline
    }
    // A gap between groups of rows.
    component Gap: Item { Layout.columnSpan: 2; implicitHeight: 3 }
    // A name, a thin bar and the number, for one level as formulas know it.
    component Meter: Row {
        id: meter
        property string label
        property real value
        // Levels sit around 1 and peak near 2; the beat runs 0 to 1.
        property real full: 2
        property int barWidth: 26
        spacing: 4
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: meter.label
            font.family: Ui.mono
            font.pixelSize: overlay.valueSize
            color: Ui.textDim
            style: Text.Outline
            styleColor: overlay.outline
        }
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: meter.barWidth
            height: 2
            color: "#30ffffff"
            Rectangle {
                height: parent.height
                width: parent.width * Math.max(0, Math.min(1, meter.value / meter.full))
                color: meter.full === 1 ? "white" : theme.accent
            }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: 26
            text: meter.value.toFixed(2)
            font.family: Ui.mono
            font.pixelSize: overlay.valueSize
            color: Ui.text
            style: Text.Outline
            styleColor: overlay.outline
        }
    }

    GridLayout {
        id: grid
        x: overlay.padX
        y: overlay.padY
        width: Math.min(implicitWidth, overlay.parent.width - 32 - 2 * overlay.padX)
        columns: 2
        columnSpacing: 10
        rowSpacing: 2

        Name { text: qsTr("Preset") }
        Value { text: overlay.presetLine }
        Name { text: qsTr("Layers") }
        Value { text: overlay.layerLine }
        Name { text: qsTr("File"); visible: overlay.stats.path.length > 0 }
        Value { text: overlay.stats.path; visible: overlay.stats.path.length > 0; elide: Text.ElideMiddle }
        Name { text: qsTr("Problems"); visible: overlay.problemLine.length > 0; color: Ui.danger }
        Value { text: overlay.problemLine; visible: overlay.problemLine.length > 0; color: Ui.danger }
        Name { text: qsTr("Next") }
        Value { text: overlay.nextLine }
        Name { text: qsTr("Changing"); visible: overlay.changeLine.length > 0 }
        Value { text: overlay.changeLine; visible: overlay.changeLine.length > 0; color: Ui.warning }

        Gap {}
        Name { text: qsTr("Frame") }
        Value { text: overlay.drawingLine }
        Name { text: qsTr("Load") }
        Value { text: overlay.engineLine }
        Name { text: qsTr("Size") }
        Value { text: overlay.sizeLine }
        Name { text: qsTr("Graphics") }
        Value { text: overlay.graphicsLine }
        Name { text: qsTr("Visitors") }
        Value { text: overlay.visitorLine }

        Gap {}
        Name { text: qsTr("Sound") }
        Value { text: overlay.soundLine }
        Name { text: qsTr("Tempo") }
        Value { text: overlay.tempoLine }
        Name { text: qsTr("Track"); visible: overlay.trackLine.length > 0 }
        Value { text: overlay.trackLine; visible: overlay.trackLine.length > 0 }
        // By the names formulas use, so a number here can be typed there.
        Name { text: qsTr("Levels") }
        Row {
            spacing: 10
            Layout.alignment: Qt.AlignVCenter
            Repeater {
                model: ["bass", "mid", "treb", "vol", "beat"]
                Meter {
                    required property string modelData
                    label: modelData
                    value: overlay.stats[modelData] || 0
                    full: modelData === "beat" ? 1 : 2
                }
            }
        }
        // The six narrow ranges, lowest first, smoothed as their _att twins.
        Name { text: qsTr("Bands") }
        Row {
            spacing: 10
            Layout.alignment: Qt.AlignVCenter
            Repeater {
                model: overlay.stats.bands
                Meter {
                    required property var modelData
                    label: modelData.name
                    value: modelData.level
                    barWidth: 18
                }
            }
        }

        Gap {}
        Name { text: qsTr("Colors") }
        Value { text: overlay.colorLine }
    }
}
