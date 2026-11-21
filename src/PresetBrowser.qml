import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The list of presets down the left side: type to find one, Enter to show
// it. Under the search, filters by color and by what a preset is made of;
// kept to, the presets change among those that match.
SidePanel {
    id: browser
    title: qsTr("Presets")
    scope: backend.filtering ? qsTr("%1 of %2").arg(backend.matchCount).arg(Ui.count(backend.presets.length, qsTr("1 preset"), qsTr("%1 presets")))
                             : Ui.count(backend.presets.length, qsTr("1 preset"), qsTr("%1 presets"))
    pictureOnLeft: false
    readonly property bool searching: search.editing
    // A preset clicked in the list, after which the keys go back to the
    // shortcuts.
    signal picked()

    readonly property var shown: {
        const needle = search.text.trim().toLowerCase()
        return backend.presets.filter(function (p) {
            return p.matches && (needle.length === 0 || p.name.toLowerCase().indexOf(needle) >= 0)
        })
    }

    // A round swatch for a family of colors: one color, or several round
    // the circle.
    component Swatch: AbstractButton {
        id: swatch
        required property var modelData
        readonly property var family: modelData
        readonly property bool chosen: backend.filterColor === family.name
        implicitWidth: 26
        implicitHeight: 26
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        onClicked: backend.filterColor = chosen ? "" : family.name
        background: Rectangle {
            radius: width / 2
            color: "transparent"
            border.width: 2
            border.color: swatch.chosen ? Ui.text : swatch.hovered ? Ui.textFaint : "transparent"
        }
        contentItem: Canvas {
            id: dot
            readonly property var colors: swatch.family.colors
            onColorsChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const r = width / 2 - 5
                ctx.beginPath()
                ctx.arc(width / 2, height / 2, r, 0, 2 * Math.PI)
                if (colors.length > 1) {
                    const g = ctx.createConicalGradient(width / 2, height / 2, 0)
                    for (let i = 0; i <= colors.length; ++i)
                        g.addColorStop(i / colors.length, colors[i % colors.length])
                    ctx.fillStyle = g
                } else {
                    ctx.fillStyle = colors.length > 0 ? colors[0] : "#808080"
                }
                ctx.fill()
            }
        }
    }

    // A feature to filter by, lit while it is.
    component Chip: AbstractButton {
        id: chip
        required property var modelData
        readonly property var feature: modelData
        readonly property bool chosen: backend.filterFeatures.indexOf(feature.name) >= 0
        implicitWidth: chipLabel.implicitWidth + 20
        implicitHeight: 24
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        onClicked: backend.toggleFeature(feature.name)
        background: Rectangle {
            radius: Math.min(height / 2, Math.max(4, theme.radius))
            color: chip.chosen ? theme.accent : chip.hovered ? Ui.fieldHover : Ui.field
        }
        contentItem: Label {
            id: chipLabel
            text: chip.feature.label
            font.pixelSize: Ui.small
            font.weight: chip.chosen ? Font.DemiBold : Font.Normal
            color: chip.chosen ? theme.accentForeground : Ui.textDim
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    function focusSearch() {
        search.take()
    }
    function pick(name) {
        win.guarded(function () { backend.select(name) })
    }

    tools: IconButton { glyph: "plus"; tip: qsTr("New preset"); onClicked: win.newPreset() }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        CompactField {
            id: search
            Layout.fillWidth: true
            Layout.margins: 12
            mono: false
            placeholder: qsTr("Search presets")
            inset: 32
            onTextChanged: list.currentIndex = 0
            onStepped: function (direction) {
                if (direction > 0)
                    list.incrementCurrentIndex()
                else
                    list.decrementCurrentIndex()
            }
            onAccepted: if (list.currentIndex >= 0 && list.currentIndex < browser.shown.length)
                            browser.pick(browser.shown[list.currentIndex].name)
            Glyph { x: 10; anchors.verticalCenter: parent.verticalCenter; name: "search"; size: 14; color: Ui.textFaint }
        }

        Row {
            Layout.leftMargin: 9
            Layout.rightMargin: 9
            spacing: 2
            Repeater {
                model: backend.colorFamilies
                delegate: Swatch {}
            }
        }
        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: 8
            spacing: 6
            Repeater {
                model: backend.featureList
                delegate: Chip {}
            }
        }
        ColumnLayout {
            visible: backend.filtering
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: 8
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: backend.matchCount === 0 ? qsTr("Nothing matches")
                        : Ui.count(backend.matchCount, qsTr("1 matches"), qsTr("%1 match"))
                    font.pixelSize: Ui.small
                    color: Ui.textDim
                }
                TextButton { text: qsTr("Clear"); implicitHeight: 24; onClicked: backend.clearFilter() }
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Cycle through these only")
                    font.pixelSize: Ui.small
                    color: Ui.text
                    elide: Text.ElideRight
                }
                Toggle {
                    checked: backend.keepToFilter
                    enabled: backend.matchCount > 0
                    onToggled: backend.keepToFilter = checked
                }
            }
        }
        Item { Layout.preferredHeight: 8 }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Ui.line }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 8
            clip: true
            model: browser.shown
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            Label {
                visible: browser.shown.length === 0
                anchors.centerIn: parent
                width: parent.width - 32
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: search.text.trim().length > 0 ? qsTr("No preset by that name here") : qsTr("No preset has all of these")
                font.pixelSize: Ui.small
                color: Ui.textFaint
            }
            delegate: Item {
                id: entry
                required property var modelData
                required property int index
                width: list.width
                height: 32
                readonly property bool current: modelData.name === backend.currentName
                Rectangle {
                    anchors.fill: parent
                    anchors.rightMargin: 8
                    radius: Math.min(4, theme.radius)
                    color: entry.current ? Ui.fieldActive : list.currentIndex === entry.index && search.editing ? Ui.fieldHover
                         : hover.hovered ? Ui.hover : "transparent"
                    // The preset on screen.
                    Rectangle {
                        visible: entry.current
                        width: 3
                        height: parent.height - 12
                        anchors.verticalCenter: parent.verticalCenter
                        radius: Math.min(1.5, theme.radius)
                        color: theme.accent
                    }
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 18
                    spacing: 8
                    Label {
                        Layout.fillWidth: true
                        text: entry.modelData.name
                        elide: Text.ElideRight
                        font.pixelSize: Ui.body
                        color: entry.current || hover.hovered ? Ui.text : Ui.textDim
                        font.weight: entry.current ? Font.DemiBold : Font.Normal
                    }
                    Label {
                        visible: !entry.modelData.builtin
                        text: entry.modelData.shadowsBuiltin ? qsTr("your copy") : qsTr("yours")
                        font.pixelSize: Ui.tiny
                        color: theme.accent
                    }
                }
                HoverHandler { id: hover }
                TapHandler {
                    onTapped: {
                        browser.picked()
                        browser.pick(entry.modelData.name)
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Ui.line }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 8
            TextButton { Layout.fillWidth: true; text: qsTr("New"); onClicked: win.newPreset() }
            TextButton { Layout.fillWidth: true; text: qsTr("Delete"); enabled: backend.currentIsYours; onClicked: win.askDelete() }
        }
    }
}
