import QtQuick

// A one-line text box small enough to sit in a row of settings. Formulas
// and numbers are set in the fixed-width font; a name is not.
Rectangle {
    id: box
    property alias text: input.text
    property bool invalid: false
    property bool mono: true
    property string placeholder
    // Room on the leading side, for an icon laid over the box.
    property real inset: 9
    property int textSize: Ui.body
    property int textWeight: Font.Normal
    readonly property bool editing: input.activeFocus
    signal committed(string text)
    signal cancelled()
    // Enter, and the arrow keys a list beside the box may want.
    signal accepted()
    signal stepped(int direction)

    function take() {
        input.forceActiveFocus()
        input.selectAll()
    }

    implicitHeight: Ui.control
    radius: Math.min(4, theme.radius)
    color: input.activeFocus ? Ui.fieldActive : hover.hovered ? Ui.fieldHover : Ui.field
    border.width: 1
    border.color: invalid ? Ui.danger : input.activeFocus ? theme.accent : "transparent"
    clip: true

    HoverHandler { id: hover; cursorShape: Qt.IBeamCursor }

    Text {
        anchors.fill: input
        verticalAlignment: Text.AlignVCenter
        visible: input.text.length === 0
        text: box.placeholder
        color: Ui.textFaint
        font: input.font
        elide: Text.ElideRight
    }
    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: box.inset
        anchors.rightMargin: 9
        verticalAlignment: TextInput.AlignVCenter
        color: Ui.text
        selectionColor: theme.accent
        selectedTextColor: theme.accentForeground
        selectByMouse: true
        font.family: box.mono ? Ui.mono : Qt.application.font.family
        font.pixelSize: box.textSize
        font.weight: box.textWeight
        onEditingFinished: box.committed(text)
        onAccepted: box.accepted()
        Keys.onDownPressed: box.stepped(1)
        Keys.onUpPressed: box.stepped(-1)
        Keys.onEscapePressed: { box.cancelled(); focus = false }
    }
}
