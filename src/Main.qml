import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Trance

ApplicationWindow {
    id: win
    width: 1280
    height: 800
    minimumWidth: 640
    minimumHeight: 420
    visible: true
    title: backend.currentName.length > 0 ? backend.currentName + " — Trance" : "Trance"
    color: backend.glass ? "transparent" : "black"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    property bool browserVisible: false
    // The right side holds one panel at a time: "editor" for the preset
    // that is showing, "settings" for the app, or "" for neither.
    property string sidePanel: ""
    readonly property bool editorVisible: sidePanel === "editor"
    readonly property bool settingsVisible: sidePanel === "settings"
    property var pendingAction: null
    // Dialogs get a set width. Left to size themselves, one whose text is
    // wider than its row of buttons chases its own button box, which Qt
    // reports as a binding loop every time the text changes.
    readonly property real dialogWidth: Math.min(420, width - 48)
    // Letter keys belong to the text box while one has the cursor.
    readonly property bool typing: activeFocusItem !== null && activeFocusItem.hasOwnProperty("cursorPosition")
    readonly property bool fullScreen: visibility === Window.FullScreen

    onEditorVisibleChanged: backend.editing = editorVisible
    // The levels are only worth announcing while something shows them.
    Binding { target: backend; property: "metering"; value: win.sidePanel.length > 0 || backend.debug }

    function toggleSide(name) { sidePanel = sidePanel === name ? "" : name }
    function toggleBrowser() {
        browserVisible = !browserVisible
        if (browserVisible)
            browser.focusSearch()
    }
    function toggleFullScreen() { visibility = fullScreen ? Window.Windowed : Window.FullScreen }

    // Runs the action now, or after asking what to do with unsaved changes.
    function guarded(action) {
        if (backend.dirty) {
            pendingAction = action
            discardDialog.open()
        } else {
            action()
        }
    }
    function toast(message) {
        toastLabel.text = message
        toastBox.opacity = 1
        toastTimer.restart()
    }
    function askName(title, initial, accept) {
        nameDialog.title = title
        nameField.text = initial
        nameDialog.accept = accept
        nameDialog.open()
        nameField.selectAll()
        nameField.forceActiveFocus()
    }
    function saveAs() {
        askName(qsTr("Save as"), backend.currentName, function (name) { backend.saveAs(name) })
    }
    function newPreset() {
        guarded(function () {
            askName(qsTr("New preset"), "", function (name) {
                if (backend.newPreset(name))
                    win.sidePanel = "editor"
            })
        })
    }
    function quit() {
        guarded(function () { Qt.quit() })
    }

    onClosing: function (close) {
        if (backend.dirty) {
            close.accepted = false
            quit()
        }
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.quit() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.guarded(function () { backend.next() }) }
    Shortcut { sequence: "Backspace"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.guarded(function () { backend.previous() }) }
    Shortcut { sequence: "R"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.guarded(function () { backend.random() }) }
    Shortcut {
        sequence: "L"; context: Qt.ApplicationShortcut; enabled: !win.typing
        onActivated: {
            backend.locked = !backend.locked
            win.toast(backend.locked ? qsTr("Holding this preset") : qsTr("Changing presets again"))
        }
    }
    Shortcut {
        sequences: ["P", "/"]; context: Qt.ApplicationShortcut; enabled: !win.typing
        onActivated: win.toggleBrowser()
    }
    Shortcut {
        sequence: "X"; context: Qt.ApplicationShortcut; enabled: !win.typing
        onActivated: win.guarded(function () {
            if (!backend.mashup())
                win.toast(qsTr("No mash-up came together. Try again."))
        })
    }
    Shortcut { sequence: "E"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.toggleSide("editor") }
    Shortcut { sequence: "S"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.toggleSide("settings") }
    Shortcut { sequence: "Ctrl+,"; context: Qt.ApplicationShortcut; onActivated: win.toggleSide("settings") }
    Shortcut { sequence: "D"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: backend.debug = !backend.debug }
    Shortcut { sequence: "Ctrl+Tab"; context: Qt.ApplicationShortcut; enabled: win.editorVisible; onActivated: editor.cycleTab(1) }
    Shortcut { sequence: "Ctrl+Shift+Tab"; context: Qt.ApplicationShortcut; enabled: win.editorVisible; onActivated: editor.cycleTab(-1) }
    Shortcut {
        sequences: ["F", "F11"]; context: Qt.ApplicationShortcut; enabled: !win.typing
        onActivated: win.toggleFullScreen()
    }
    Shortcut {
        sequence: "T"; context: Qt.ApplicationShortcut; enabled: !win.typing
        onActivated: {
            const name = backend.cyclePalette()
            win.toast(name.length > 0 ? qsTr("Colors: %1").arg(name) : qsTr("Colors: each preset's own"))
        }
    }
    Shortcut {
        sequence: "G"; context: Qt.ApplicationShortcut; enabled: !win.typing
        onActivated: {
            backend.glass = !backend.glass
            win.toast(backend.glass ? qsTr("Glass on") : qsTr("Glass off"))
        }
    }
    Shortcut {
        sequence: "N"; context: Qt.ApplicationShortcut; enabled: !win.typing
        onActivated: {
            const style = backend.cycleTitleStyle()
            win.toast(style === "corner" ? qsTr("Track titles in the corner")
                    : style === "picture" ? qsTr("Track titles in the picture") : qsTr("Track titles off"))
        }
    }
    Shortcut {
        sequence: "M"; context: Qt.ApplicationShortcut; enabled: !win.typing
        onActivated: {
            backend.source = backend.source === "microphone" ? "system" : "microphone"
            win.toast(backend.source === "system" ? qsTr("Listening to what's playing") : qsTr("Listening to the microphone"))
        }
    }
    Shortcut {
        sequences: ["+", "="]; context: Qt.ApplicationShortcut; enabled: !win.typing
        onActivated: {
            backend.sensitivity = backend.sensitivity * 1.15
            win.toast(qsTr("Sensitivity %1%").arg(Math.round(backend.sensitivity * 100)))
        }
    }
    Shortcut {
        sequence: "-"; context: Qt.ApplicationShortcut; enabled: !win.typing
        onActivated: {
            backend.sensitivity = backend.sensitivity / 1.15
            win.toast(qsTr("Sensitivity %1%").arg(Math.round(backend.sensitivity * 100)))
        }
    }
    Shortcut { sequences: [StandardKey.Save]; context: Qt.ApplicationShortcut; onActivated: backend.save() }
    Shortcut { sequence: "Ctrl+Shift+S"; context: Qt.ApplicationShortcut; onActivated: win.saveAs() }
    Shortcut { sequence: "Ctrl+N"; context: Qt.ApplicationShortcut; onActivated: win.newPreset() }
    Shortcut { sequences: [StandardKey.Undo]; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: backend.undo() }
    Shortcut { sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: backend.redo() }
    Shortcut {
        sequence: "Esc"; context: Qt.ApplicationShortcut
        onActivated: {
            if (win.typing)
                content.forceActiveFocus()
            else if (win.helpVisible)
                win.helpVisible = false
            else if (win.browserVisible)
                win.browserVisible = false
            else if (win.sidePanel.length > 0)
                win.sidePanel = ""
            else if (win.fullScreen)
                win.visibility = Window.Windowed
        }
    }

    Connections {
        target: backend
        // Off unless asked for: the picture changing says enough.
        function onCurrentChanged() {
            if (backend.showNames)
                win.toast(backend.currentName)
        }
        function onNotice(message) { win.toast(message) }
    }
    Connections {
        target: nowPlaying
        function onChanged() {
            if (backend.titleStyle === "corner")
                titleBox.show()
        }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        id: content
        anchors.fill: parent
        focus: true
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        PresetBrowser {
            id: browser
            visible: win.browserVisible
            width: Math.min(300, parent.width * 0.4)
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
            onCloseRequested: win.browserVisible = false
        }

        // This preset's own settings.
        EditorPanel {
            id: editor
            visible: win.editorVisible
            width: Math.min(440, parent.width * 0.5)
            anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
            onCloseRequested: win.sidePanel = ""
        }

        // The app's settings, the same whatever preset is showing.
        SettingsPanel {
            id: settings
            visible: win.settingsVisible
            width: Math.min(440, parent.width * 0.5)
            anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
            onCloseRequested: win.sidePanel = ""
        }

        Visualizer {
            id: picture
            anchors {
                left: win.browserVisible ? browser.right : parent.left
                right: win.editorVisible ? editor.left : win.settingsVisible ? settings.left : parent.right
                top: parent.top
                bottom: parent.bottom
            }

            // The pointer gets out of the way a few seconds after it stops,
            // and the controls with it.
            MouseArea {
                id: pointer
                anchors.fill: parent
                hoverEnabled: true
                property bool shown: true
                cursorShape: shown ? Qt.ArrowCursor : Qt.BlankCursor
                onPositionChanged: { shown = true; hideTimer.restart() }
                onDoubleClicked: win.toggleFullScreen()
                onClicked: content.forceActiveFocus()
                Timer { id: hideTimer; interval: 2500; running: true; onTriggered: pointer.shown = false }
            }

            DebugOverlay {
                id: debug
                anchors { left: parent.left; top: parent.top; margins: 16 }
                visible: backend.debug
                picture: picture
            }

            // Short messages, and preset names when those are wanted.
            Rectangle {
                id: toastBox
                // Under the debug overlay when that is up, not across it.
                anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: debug.visible ? debug.height + 28 : 20 }
                width: Math.min(toastLabel.implicitWidth + 32, parent.width - 48)
                height: toastLabel.implicitHeight + 16
                radius: Math.min(height / 2, theme.radius)
                color: "#d0101014"
                border.color: Ui.line
                opacity: 0
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: 250 } }
                Label {
                    id: toastLabel
                    anchors.centerIn: parent
                    width: Math.min(implicitWidth, parent.width - 32)
                    elide: Text.ElideRight
                    font.pixelSize: Ui.body
                    font.weight: Font.DemiBold
                    color: Ui.text
                }
                Timer { id: toastTimer; interval: 2200; onTriggered: toastBox.opacity = 0 }
            }

            // The track that just started.
            Column {
                id: titleBox
                anchors { left: parent.left; bottom: parent.bottom; margins: 32; bottomMargin: 76 }
                width: parent.width - 64
                spacing: 2
                opacity: 0
                visible: opacity > 0
                function show() { opacity = 1; titleTimer.restart() }
                Behavior on opacity { NumberAnimation { duration: 900 } }
                Timer { id: titleTimer; interval: 7000; onTriggered: titleBox.opacity = 0 }
                Label {
                    width: parent.width
                    text: nowPlaying.title
                    elide: Text.ElideRight
                    font.pixelSize: Math.max(22, Math.min(44, picture.height / 18))
                    font.weight: Font.Bold
                    color: "white"
                    style: Text.Raised
                    styleColor: "#c0000000"
                }
                Label {
                    width: parent.width
                    text: nowPlaying.artist
                    elide: Text.ElideRight
                    visible: text.length > 0
                    font.pixelSize: Math.max(15, Math.min(24, picture.height / 32))
                    color: theme.accent
                    style: Text.Raised
                    styleColor: "#c0000000"
                }
            }

            // What stands between the music and the picture, if anything.
            Rectangle {
                anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 76 }
                width: Math.min(statusLabel.implicitWidth + 32, parent.width - 48)
                height: statusLabel.implicitHeight + 16
                radius: Math.min(height / 2, theme.radius)
                color: backend.audioError.length > 0 ? "#d0401010" : "#d0101014"
                border.color: Ui.line
                visible: statusLabel.text.length > 0 && titleBox.opacity === 0
                Label {
                    id: statusLabel
                    anchors.centerIn: parent
                    width: parent.width - 32
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: Ui.text
                    font.pixelSize: Ui.body
                    text: backend.audioError.length > 0 ? backend.audioError
                        : quietTimer.waited && backend.silent
                            ? (backend.source === "system" ? qsTr("Waiting for music. Play something and the picture follows.")
                               : backend.source === "microphone" ? qsTr("Waiting for sound from the microphone.")
                               : qsTr("Waiting for sound from %1.").arg(backend.sourceLabel()))
                        : hintTimer.running ? qsTr("Press ? for the shortcuts") : ""
                }
                // Silence between songs is not worth a message; lasting silence is.
                Timer {
                    id: quietTimer
                    property bool waited: false
                    interval: 4000
                    running: backend.silent
                    onRunningChanged: if (!running) waited = false
                    onTriggered: waited = true
                }
                Timer { id: hintTimer; interval: 5000; running: true }
            }

            ControlBar {
                id: controls
                anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 20 }
                fullScreen: win.fullScreen
                opacity: pointer.shown || hovered ? 1 : 0
                visible: opacity > 0 && parent.width >= width + 32
                Behavior on opacity { NumberAnimation { duration: 200 } }
                onBrowse: win.toggleBrowser()
                onPrevious: win.guarded(function () { backend.previous() })
                onRandom: win.guarded(function () { backend.random() })
                onNext: win.guarded(function () { backend.next() })
                onEdit: win.toggleSide("editor")
                onSettings: win.toggleSide("settings")
                onHelp: win.helpVisible = !win.helpVisible
                onToggleFullScreen: win.toggleFullScreen()
            }
        }

        HelpOverlay {
            anchors.fill: parent
            visible: win.helpVisible
            onDismissed: win.helpVisible = false
        }
    }

    Dialog {
        id: discardDialog
        anchors.centerIn: parent
        width: win.dialogWidth
        modal: true
        title: qsTr("Unsaved changes")
        Material.roundedScale: theme.radius
        onClosed: content.forceActiveFocus()
        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("%1 has changes that aren't saved.").arg(backend.currentName)
        }
        footer: DialogButtonBox {
            Button {
                text: qsTr("Save")
                flat: true
                Material.roundedScale: theme.radius
                onClicked: {
                    discardDialog.close()
                    if (backend.save() && win.pendingAction)
                        win.pendingAction()
                    win.pendingAction = null
                }
            }
            Button {
                text: qsTr("Discard")
                flat: true
                Material.roundedScale: theme.radius
                onClicked: {
                    discardDialog.close()
                    backend.revert()
                    // A preset that was never saved has nothing to go back to.
                    const action = win.pendingAction
                    win.pendingAction = null
                    if (backend.dirty)
                        backend.select(backend.presets.length > 0 ? backend.presets[0].name : "")
                    if (action)
                        action()
                }
            }
            Button {
                text: qsTr("Cancel")
                flat: true
                Material.roundedScale: theme.radius
                onClicked: {
                    win.pendingAction = null
                    discardDialog.close()
                }
            }
        }
    }

    Dialog {
        id: nameDialog
        anchors.centerIn: parent
        width: win.dialogWidth
        modal: true
        Material.roundedScale: theme.radius
        property var accept: null
        onClosed: content.forceActiveFocus()
        function confirm() {
            const name = nameField.text.trim()
            if (name.length === 0)
                return
            close()
            if (accept)
                accept(name)
        }
        TextField {
            id: nameField
            width: parent.width
            placeholderText: qsTr("Name")
            Material.roundedScale: Math.min(4, theme.radius)
            onAccepted: nameDialog.confirm()
        }
        footer: DialogButtonBox {
            Button { text: qsTr("OK"); flat: true; Material.roundedScale: theme.radius; onClicked: nameDialog.confirm() }
            Button { text: qsTr("Cancel"); flat: true; Material.roundedScale: theme.radius; onClicked: nameDialog.close() }
        }
    }

    Dialog {
        id: deleteDialog
        anchors.centerIn: parent
        width: win.dialogWidth
        modal: true
        title: qsTr("Delete preset")
        Material.roundedScale: theme.radius
        onClosed: content.forceActiveFocus()
        function ask() { open() }
        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("Delete %1? This can't be undone.").arg(backend.currentName)
        }
        footer: DialogButtonBox {
            Button {
                text: qsTr("Delete")
                flat: true
                Material.roundedScale: theme.radius
                onClicked: { deleteDialog.close(); backend.deleteCurrent() }
            }
            Button { text: qsTr("Cancel"); flat: true; Material.roundedScale: theme.radius; onClicked: deleteDialog.close() }
        }
    }

    // Panels reach the window's helpers through these.
    function askDelete() { deleteDialog.ask() }
    function showHelp(item, title, body, detail) { helpTip.show(item, title, body, detail) }
    function hideHelp(item) { helpTip.hide(item) }

    HelpTip { id: helpTip }
}
