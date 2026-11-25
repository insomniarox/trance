import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A list to pick one thing from. Given a list of objects, `textRole` names
// what to show, and `sectionRole` what to group by: each group gets its
// heading once, above its first entry.
ComboBox {
    id: picker
    property string sectionRole
    implicitWidth: 208
    implicitHeight: Ui.control
    focusPolicy: Qt.NoFocus
    hoverEnabled: true
    font.pixelSize: Ui.body
    leftPadding: 10
    rightPadding: 30
    topPadding: 0
    bottomPadding: 0

    function textAt(index) {
        const item = model[index]
        return textRole.length > 0 && item !== undefined && item !== null ? item[textRole] : String(item)
    }
    function sectionAt(index) {
        if (sectionRole.length === 0 || index < 0)
            return ""
        const item = model[index]
        return item ? (item[sectionRole] || "") : ""
    }

    background: Rectangle {
        radius: Math.min(4, theme.radius)
        color: picker.popup.visible ? Ui.fieldActive : picker.hovered ? Ui.fieldHover : Ui.field
    }
    contentItem: Label {
        text: picker.displayText
        font: picker.font
        color: Ui.text
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
    indicator: Glyph {
        x: picker.mirrored ? 9 : picker.width - width - 9
        y: (picker.height - height) / 2
        name: "chevron-down"
        size: 14
        color: Ui.textDim
    }

    delegate: ItemDelegate {
        id: entry
        required property int index
        readonly property string section: picker.sectionAt(index)
        readonly property bool headed: section.length > 0 && section !== picker.sectionAt(index - 1)
        readonly property bool chosen: picker.currentIndex === index
        width: ListView.view ? ListView.view.width : picker.width
        height: 30 + (headed ? 28 : 0)
        padding: 0
        hoverEnabled: true
        background: null
        contentItem: Item {
            Label {
                visible: entry.headed
                x: 10
                y: 10
                text: entry.section
                font.pixelSize: Ui.tiny
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 0.8
                color: Ui.textFaint
            }
            Rectangle {
                y: entry.headed ? 28 : 0
                width: parent.width
                height: 30
                radius: Math.min(4, theme.radius)
                color: picker.highlightedIndex === entry.index ? Ui.fieldHover : "transparent"
                Label {
                    anchors { left: parent.left; right: tick.left; leftMargin: 10; rightMargin: 6; verticalCenter: parent.verticalCenter }
                    text: picker.textAt(entry.index)
                    font.pixelSize: Ui.body
                    color: Ui.text
                    elide: Text.ElideRight
                }
                Glyph {
                    id: tick
                    anchors { right: parent.right; rightMargin: 9; verticalCenter: parent.verticalCenter }
                    name: "check"
                    size: 14
                    color: theme.accent
                    visible: entry.chosen
                }
            }
        }
    }

    popup: Popup {
        // Drawn in the window like everything else, whatever kind of popup
        // the style would pick, so it looks and closes the same everywhere.
        popupType: Popup.Item
        // Takes the keys while it is open, so Esc closes the list and not
        // the panel it is in.
        focus: true
        x: picker.mirrored ? 0 : picker.width - width
        y: picker.height + 4
        width: Math.max(picker.width, 240)
        height: Math.min(contentItem.implicitHeight + 8, 380)
        padding: 4
        background: Rectangle {
            radius: Math.min(4, theme.radius)
            color: Ui.popup
            border.width: 1
            border.color: Ui.line
        }
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: picker.delegateModel
            currentIndex: picker.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
        }
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 90 } }
        exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 90 } }
    }
}
