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

    // The preset and the list it was just put in or taken out of, lit for
    // a moment so the change is seen where it happened.
    property string flashPreset
    property string flashList
    function flash(listName, preset) {
        flashPreset = preset
        flashList = listName
        flashTimer.restart()
    }
    Timer {
        id: flashTimer
        interval: 450
        onTriggered: {
            browser.flashPreset = ""
            browser.flashList = ""
        }
    }

    // What the presets are picked by. While it stays the same, a change to
    // the presets themselves (a star, a list, a save) leaves the list
    // where it was; a new search or filter starts it from the top.
    // A function rather than a binding, so it is never read before it has
    // caught up with a change.
    function query() {
        return [search.text.trim().toLowerCase(), backend.filterColor,
                backend.filterFeatures.join(","), backend.filterList].join("|")
    }
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

    // One of the user's lists, picked like a filter. Right-click to rename
    // or delete it; Favorites is always there.
    component ListChip: AbstractButton {
        id: listChip
        required property var modelData
        required property int index
        readonly property bool chosen: backend.filterList === modelData.name
        readonly property bool flashing: browser.flashList === modelData.name
        implicitWidth: listLabel.implicitWidth + listCount.implicitWidth + 26
        implicitHeight: 24
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        onClicked: backend.filterList = chosen ? "" : modelData.name
        background: Rectangle {
            radius: Math.min(height / 2, Math.max(4, theme.radius))
            color: listChip.chosen ? theme.accent : listChip.hovered ? Ui.fieldHover : Ui.field
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: listChip.chosen ? theme.accentForeground : theme.accent
                opacity: listChip.flashing ? 0.4 : 0
                Behavior on opacity { NumberAnimation { duration: listChip.flashing ? 80 : 600; easing.type: Easing.OutQuad } }
            }
        }
        contentItem: Row {
            spacing: 6
            leftPadding: 10
            Label {
                id: listLabel
                anchors.verticalCenter: parent.verticalCenter
                text: listChip.modelData.name
                font.pixelSize: Ui.small
                font.weight: listChip.chosen ? Font.DemiBold : Font.Normal
                color: listChip.chosen ? theme.accentForeground : Ui.textDim
            }
            Label {
                id: listCount
                anchors.verticalCenter: parent.verticalCenter
                text: listChip.modelData.count
                font.pixelSize: Ui.tiny
                color: listChip.chosen ? theme.accentForeground : Ui.textFaint
                opacity: listChip.chosen ? 0.75 : 1
            }
        }
        TapHandler {
            acceptedButtons: Qt.RightButton
            enabled: listChip.index > 0
            onTapped: function (point) {
                const name = listChip.modelData.name
                menu.show(listChip, point.position.x, point.position.y, [
                    { text: qsTr("Rename…"), run: function () {
                        win.askName(qsTr("Rename list"), name, function (to) { backend.renameList(name, to) })
                    } },
                    { text: qsTr("Delete…"), run: function () {
                        win.confirm(qsTr("Delete list"), qsTr("Delete the list %1? The presets in it stay.").arg(name),
                                    qsTr("Delete"), function () { backend.deleteList(name) })
                    } }
                ])
            }
        }
    }

    // Every list, ticked where the preset is in it, and a new one.
    function listEntries(preset) {
        const entries = backend.presetLists.map(function (list) {
            return { text: list.name, ticked: preset.lists.indexOf(list.name) >= 0,
                     run: function () {
                         if (backend.toggleInList(list.name, preset.name))
                             browser.flash(list.name, preset.name)
                     } }
        })
        entries.push({ line: true })
        entries.push({ text: qsTr("New list…"), run: function () {
            win.askName(qsTr("New list"), "", function (name) { backend.newList(name, preset.name) })
        } })
        return entries
    }

    PopupMenu { id: menu }

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
        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: 8
            spacing: 6
            Repeater {
                model: backend.presetLists
                delegate: ListChip {}
            }
            IconButton {
                implicitWidth: 24
                implicitHeight: 24
                glyph: "plus"
                glyphSize: 14
                tip: qsTr("New list")
                onClicked: win.askName(qsTr("New list"), "", function (name) { backend.newList(name) })
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
            // Given a new model, a ListView goes back to the top, and the
            // presets come as a new one every time any of them changes.
            readonly property int rowHeight: 32
            property string shownFor
            function refill() {
                const y = contentY
                const same = shownFor === browser.query()
                shownFor = browser.query()
                model = browser.shown
                contentY = same ? Math.min(y, Math.max(0, browser.shown.length * rowHeight - height)) : 0
            }
            Component.onCompleted: refill()
            Connections { target: browser; function onShownChanged() { list.refill() } }
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            Label {
                visible: browser.shown.length === 0
                anchors.centerIn: parent
                width: parent.width - 32
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: search.text.trim().length > 0 ? qsTr("No preset by that name here")
                    : backend.filterList.length > 0 && backend.matchCount === 0
                      ? qsTr("Nothing in %1 yet: star a preset, or put it in a list with the button beside the star").arg(backend.filterList)
                    : qsTr("No preset has all of these")
                font.pixelSize: Ui.small
                color: Ui.textFaint
            }
            delegate: Item {
                id: entry
                required property var modelData
                required property int index
                width: list.width
                height: list.rowHeight
                readonly property bool current: modelData.name === backend.currentName
                readonly property bool flashing: browser.flashPreset === modelData.name
                // The lists it is in besides Favorites, which the star shows.
                readonly property var inLists: modelData.lists.filter(function (name) {
                    return name !== backend.presetLists[0].name
                })
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
                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        color: theme.accent
                        opacity: entry.flashing ? 0.3 : 0
                        Behavior on opacity { NumberAnimation { duration: entry.flashing ? 80 : 600; easing.type: Easing.OutQuad } }
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
                    // In other lists, and how many; shows while pointed at
                    // when in none. Opens the same menu as a right-click.
                    AbstractButton {
                        id: listMark
                        implicitWidth: listMarkRow.implicitWidth + 6
                        implicitHeight: 20
                        focusPolicy: Qt.NoFocus
                        hoverEnabled: true
                        opacity: entry.inLists.length > 0 || hover.hovered ? 1 : 0
                        onClicked: menu.show(listMark, 0, height, browser.listEntries(entry.modelData))
                        contentItem: Row {
                            id: listMarkRow
                            spacing: 3
                            leftPadding: 3
                            readonly property color tint: entry.inLists.length > 0 ? theme.accent
                                                        : listMark.hovered ? Ui.text : Ui.textFaint
                            Glyph {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "list"
                                size: 14
                                color: listMarkRow.tint
                            }
                            Label {
                                visible: entry.inLists.length > 0
                                anchors.verticalCenter: parent.verticalCenter
                                text: entry.inLists.length
                                font.pixelSize: Ui.tiny
                                color: listMarkRow.tint
                            }
                        }
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: entry.inLists.length > 0 ? qsTr("In %1").arg(entry.inLists.join(", "))
                                                               : qsTr("Put in a list")
                    }
                    // In Favorites, or not yet, which shows while pointed at.
                    AbstractButton {
                        id: star
                        implicitWidth: 20
                        implicitHeight: 20
                        focusPolicy: Qt.NoFocus
                        hoverEnabled: true
                        opacity: entry.modelData.favorite || hover.hovered ? 1 : 0
                        onClicked: {
                            if (backend.toggleFavorite(entry.modelData.name))
                                browser.flash(backend.presetLists[0].name, entry.modelData.name)
                        }
                        contentItem: Glyph {
                            name: "star"
                            size: 14
                            filled: entry.modelData.favorite
                            color: entry.modelData.favorite ? theme.accent : star.hovered ? Ui.text : Ui.textFaint
                        }
                    }
                }
                HoverHandler { id: hover }
                TapHandler {
                    onTapped: {
                        browser.picked()
                        browser.pick(entry.modelData.name)
                    }
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: function (point) {
                        menu.show(entry, point.position.x, point.position.y, browser.listEntries(entry.modelData))
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
