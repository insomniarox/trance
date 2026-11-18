import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The app's own settings: what it listens to, how presets come and go, and
// what is shown over the picture. They hold whatever preset is showing, and
// nothing here is saved into a preset. A preset's own settings are in the
// editor.
SidePanel {
    id: panel
    title: qsTr("Settings")
    scope: qsTr("For every preset")
    signal aboutRequested()
    tools: IconButton { glyph: "info"; tip: qsTr("About Trance"); onClicked: panel.aboutRequested() }

    // One setting: what it is on the left, the control for it on the right.
    component Setting: Item {
        id: setting
        property string label
        property string note
        // The note is something wrong, rather than what the setting does.
        property bool alert: false
        default property alias control: slot.data
        readonly property bool first: parent !== null && parent.children[0] === setting
        Layout.fillWidth: true
        implicitHeight: Math.max(Ui.row, words.implicitHeight + 22)
        Rectangle {
            visible: !setting.first
            anchors { left: parent.left; right: parent.right; leftMargin: 12; rightMargin: 12 }
            height: 1
            color: Ui.line
        }
        RowLayout {
            anchors { fill: parent; leftMargin: 12; rightMargin: 12 }
            spacing: 14
            ColumnLayout {
                id: words
                Layout.fillWidth: true
                spacing: 2
                Label {
                    Layout.fillWidth: true
                    text: setting.label
                    font.pixelSize: Ui.body
                    color: Ui.text
                    wrapMode: Text.WordWrap
                }
                Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: setting.note
                    font.pixelSize: Ui.tiny
                    color: setting.alert ? Ui.danger : Ui.textFaint
                    wrapMode: Text.WordWrap
                }
            }
            RowLayout { id: slot; spacing: 10; Layout.alignment: Qt.AlignVCenter }
        }
    }
    // Settings that belong together, under one heading.
    component Group: ColumnLayout {
        property string heading
        default property alias settings: rows.data
        Layout.fillWidth: true
        spacing: 6
        Label {
            Layout.leftMargin: 2
            text: parent.heading
            font.pixelSize: Ui.tiny
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 0.8
            color: Ui.textDim
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: rows.implicitHeight
            radius: Math.min(4, theme.radius)
            color: Ui.card
            ColumnLayout { id: rows; anchors.fill: parent; spacing: 0 }
        }
    }
    component Amount: Label {
        Layout.preferredWidth: 44
        horizontalAlignment: Text.AlignRight
        font.pixelSize: Ui.small
        color: Ui.textDim
    }

    // The list of things to listen to keeps up with applications starting
    // and stopping their sound for as long as it is on screen.
    Timer {
        interval: 3000
        repeat: true
        triggeredOnStart: true
        running: panel.visible
        onTriggered: backend.refreshSources()
    }

    Flickable {
        anchors.fill: parent
        clip: true
        contentHeight: column.implicitHeight + 2 * Ui.pad
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: column
            x: Ui.pad
            y: Ui.pad
            width: parent.width - 2 * Ui.pad
            spacing: 20

            Group {
                heading: qsTr("Sound")
                Setting {
                    label: qsTr("Listen to")
                    alert: backend.audioError.length > 0
                    note: backend.audioError.length > 0 ? backend.audioError
                        : qsTr("Pick one application to follow only its sound. It keeps playing through your speakers as before.")
                    Picker {
                        // Everything, each application making sound, then
                        // the outputs and inputs there is a choice between.
                        model: backend.sources
                        textRole: "label"
                        sectionRole: "section"
                        function place() {
                            for (let i = 0; i < backend.sources.length; ++i)
                                if (backend.sources[i].id === backend.source)
                                    return i
                            return 0
                        }
                        currentIndex: place()
                        onModelChanged: currentIndex = place()
                        onActivated: backend.source = backend.sources[currentIndex].id
                        onPressedChanged: if (pressed) backend.refreshSources()
                    }
                }
                Setting {
                    label: qsTr("Sensitivity")
                    note: qsTr("How hard the picture reacts.")
                    LevelMeter { barHeight: 22 }
                    ValueSlider {
                        Layout.preferredWidth: 115
                        from: 0.2; to: 4
                        value: backend.sensitivity
                        onMoved: backend.sensitivity = value
                    }
                    Amount { text: Math.round(backend.sensitivity * 100) + "%" }
                }
            }

            Group {
                heading: qsTr("Presets")
                Setting {
                    label: qsTr("Change preset")
                    Picker {
                        readonly property var steps: [0, 15, 30, 60, 120, 300, 600]
                        model: steps.map(function (s) {
                            return s === 0 ? qsTr("Never") : s < 60 ? qsTr("Every %1 s").arg(s) : qsTr("Every %1 min").arg(s / 60)
                        })
                        currentIndex: {
                            let best = 0
                            for (let i = 0; i < steps.length; ++i)
                                if (Math.abs(steps[i] - backend.cycleSeconds) < Math.abs(steps[best] - backend.cycleSeconds))
                                    best = i
                            return best
                        }
                        onActivated: backend.cycleSeconds = steps[currentIndex]
                    }
                }
                Setting {
                    label: qsTr("Shuffle")
                    Toggle { checked: backend.shuffle; onToggled: backend.shuffle = checked }
                }
                Setting {
                    label: qsTr("Hold this preset")
                    Toggle { checked: backend.locked; onToggled: backend.locked = checked }
                }
                Setting {
                    label: qsTr("Transition")
                    Picker {
                        // In the order of the schema's transitions.
                        model: [qsTr("Mixed"), qsTr("Dissolve"), qsTr("From the middle"), qsTr("From the edges"),
                                qsTr("Sweep"), qsTr("Clouds"), qsTr("Wipe"), qsTr("Blinds"), qsTr("Tiles"),
                                qsTr("Spiral"), qsTr("Shards")]
                        currentIndex: Math.max(0, backend.schema.transitions.indexOf(backend.transition))
                        onActivated: backend.transition = backend.schema.transitions[currentIndex]
                    }
                }
                Setting {
                    label: qsTr("Blend time")
                    ValueSlider {
                        Layout.preferredWidth: 154
                        from: 0; to: 12
                        value: backend.blendSeconds
                        onMoved: backend.blendSeconds = Math.round(value * 10) / 10
                    }
                    Amount { text: backend.blendSeconds.toFixed(1) + " s" }
                }
                Setting {
                    label: qsTr("Mash-ups")
                    note: qsTr("Now and then, a preset put together from parts of others: one's motion, another's layers, a third's colors.")
                    Toggle { checked: backend.mashups; onToggled: backend.mashups = checked }
                }
                Setting {
                    label: qsTr("Visitors")
                    note: qsTr("Small extras that drop in over any preset for a while and leave again.")
                    Toggle { checked: backend.visitors; onToggled: backend.visitors = checked }
                }
            }

            Group {
                heading: qsTr("Picture")
                Setting {
                    label: qsTr("Colors")
                    note: qsTr("One palette for every preset, in place of its own. The editor shows the preset's own.")
                    Picker {
                        model: [qsTr("Each preset's own")].concat(backend.schema.palettes)
                        currentIndex: backend.schema.palettes.indexOf(backend.paletteOverride) + 1
                        onActivated: backend.paletteOverride = currentIndex === 0 ? "" : backend.schema.palettes[currentIndex - 1]
                    }
                }
                Setting {
                    label: qsTr("Colors drift")
                    note: qsTr("The palette changes by itself every half minute or so.")
                    Toggle { checked: backend.colorDrift; onToggled: backend.colorDrift = checked }
                }
                Setting {
                    label: qsTr("Glass")
                    note: qsTr("Black turns see-through, so the desktop shows behind the picture.")
                    Toggle { checked: backend.glass; onToggled: backend.glass = checked }
                }
                Setting {
                    label: qsTr("Glass opacity")
                    note: qsTr("How dark the glass is, from clear to solid black.")
                    opacity: backend.glass ? 1 : 0.4
                    ValueSlider {
                        Layout.preferredWidth: 154
                        enabled: backend.glass
                        from: 0; to: 1
                        value: backend.glassOpacity
                        onMoved: backend.glassOpacity = Math.round(value * 100) / 100
                    }
                    Amount { text: Math.round(backend.glassOpacity * 100) + "%" }
                }
                Setting {
                    label: qsTr("Detail")
                    note: qsTr("Lower detail is lighter on the graphics card.")
                    Picker {
                        readonly property var steps: [1, 0.75, 0.5]
                        model: ["100%", "75%", "50%"]
                        currentIndex: backend.quality > 0.9 ? 0 : backend.quality > 0.6 ? 1 : 2
                        onActivated: backend.quality = steps[currentIndex]
                    }
                }
            }

            Group {
                heading: qsTr("On screen")
                Setting {
                    label: qsTr("Preset names")
                    note: qsTr("Show a preset's name when it comes on.")
                    Toggle { checked: backend.showNames; onToggled: backend.showNames = checked }
                }
                Setting {
                    label: qsTr("Track titles")
                    Picker {
                        readonly property var styles: ["corner", "picture", "off"]
                        model: [qsTr("In the corner"), qsTr("In the picture"), qsTr("Off")]
                        currentIndex: Math.max(0, styles.indexOf(backend.titleStyle))
                        onActivated: backend.titleStyle = styles[currentIndex]
                    }
                }
                Setting {
                    label: qsTr("Debug overlay")
                    note: qsTr("The preset and its file, frame rate and load, the graphics card, the tempo, and every level formulas can read.")
                    Toggle { checked: backend.debug; onToggled: backend.debug = checked }
                }
            }

            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: "Trance " + Qt.application.version
                font.pixelSize: Ui.tiny
                color: Ui.textFaint
            }
        }
    }
}
