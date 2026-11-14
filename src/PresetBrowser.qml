import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The list of presets down the left side: type to filter, Enter to show.
SidePanel {
    id: browser
    title: qsTr("Presets")
    scope: Ui.count(backend.presets.length, qsTr("1 preset"), qsTr("%1 presets"))
    pictureOnLeft: false

    readonly property var shown: {
        const needle = search.text.trim().toLowerCase()
        const all = backend.presets
        return needle.length === 0 ? all : all.filter(function (p) { return p.name.toLowerCase().indexOf(needle) >= 0 })
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

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 8
            clip: true
            model: browser.shown
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
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
                TapHandler { onTapped: browser.pick(entry.modelData.name) }
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
