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
    property bool aboutVisible: false
    property bool browserVisible: false
    // The right side holds one panel at a time: "editor" for the preset
    // that is showing, "settings" for the app, or "" for neither.
    property string sidePanel: ""
    readonly property bool editorVisible: sidePanel === "editor"
    // The editor's tab, kept here because the editor itself only exists
    // while it is open.
    property int editorTab: 0
    readonly property bool settingsVisible: sidePanel === "settings"
    property var pendingAction: null
    // Letter keys belong to the text box while one has the cursor.
    readonly property bool typing: activeFocusItem !== null && activeFocusItem.hasOwnProperty("cursorPosition")
    readonly property bool fullScreen: visibility === Window.FullScreen

    onEditorVisibleChanged: backend.editing = editorVisible
    // The levels are only worth announcing while something shows them.
    Binding { target: backend; property: "metering"; value: win.sidePanel.length > 0 || backend.debug || startLogo.active }

    function toggleSide(name) { sidePanel = sidePanel === name ? "" : name }
    // The list opens and closes without taking the keyboard, so the
    // shortcuts still work while it is up; / opens it to type in.
    function toggleBrowser() { browserVisible = !browserVisible }
    function searchPresets() {
        browserVisible = true
        browser.focusSearch()
    }
    // The shortcuts and the About screen take the same place, one at a time.
    function toggleHelp() {
        helpVisible = !helpVisible
        aboutVisible = false
    }
    function toggleAbout() {
        aboutVisible = !aboutVisible
        helpVisible = false
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
        nameField.take()
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

    Shortcut { sequence: "?"; enabled: !win.typing; onActivated: win.toggleHelp() }
    Shortcut { sequence: "A"; enabled: !win.typing; onActivated: win.toggleAbout() }
    Shortcut { sequence: "Q"; enabled: !win.typing; onActivated: win.quit() }
    Shortcut { sequence: "Space"; enabled: !win.typing; onActivated: win.guarded(function () { backend.next() }) }
    Shortcut { sequence: "Backspace"; enabled: !win.typing; onActivated: win.guarded(function () { backend.previous() }) }
    Shortcut { sequence: "R"; enabled: !win.typing; onActivated: win.guarded(function () { backend.random() }) }
    Shortcut { sequence: "H"; enabled: !win.typing; onActivated: backend.toggleFavorite() }
    Shortcut {
        sequence: "L"; enabled: !win.typing
        onActivated: {
            backend.locked = !backend.locked
            win.toast(backend.locked ? qsTr("Holding this preset") : qsTr("Changing presets again"))
        }
    }
    Shortcut {
        sequence: "P"; enabled: !win.typing
        onActivated: win.toggleBrowser()
    }
    Shortcut {
        sequence: "/"; enabled: !win.typing
        onActivated: win.searchPresets()
    }
    Shortcut {
        sequence: "X"; enabled: !win.typing
        onActivated: win.guarded(function () {
            if (!backend.mashup())
                win.toast(qsTr("No mash-up came together. Try again."))
        })
    }
    Shortcut { sequence: "E"; enabled: !win.typing; onActivated: win.toggleSide("editor") }
    Shortcut { sequence: "S"; enabled: !win.typing; onActivated: win.toggleSide("settings") }
    Shortcut { sequence: "Ctrl+,"; onActivated: win.toggleSide("settings") }
    Shortcut { sequence: "D"; enabled: !win.typing; onActivated: backend.debug = !backend.debug }
    Shortcut { sequence: "Ctrl+Tab"; enabled: win.editorVisible; onActivated: win.editorTab = (win.editorTab + 1) % editor.item.tabNames.length }
    Shortcut { sequence: "Ctrl+Shift+Tab"; enabled: win.editorVisible; onActivated: win.editorTab = (win.editorTab + editor.item.tabNames.length - 1) % editor.item.tabNames.length }
    Shortcut {
        sequences: ["F", "F11"]; enabled: !win.typing
        onActivated: win.toggleFullScreen()
    }
    Shortcut {
        sequence: "T"; enabled: !win.typing
        onActivated: {
            const name = backend.cyclePalette()
            win.toast(name.length === 0 ? qsTr("Colors: each preset's own")
                      : backend.editing ? qsTr("Colors: %1, once the editor is closed").arg(name)
                      : qsTr("Colors: %1").arg(name))
        }
    }
    Shortcut {
        sequence: "G"; enabled: !win.typing
        onActivated: {
            backend.glass = !backend.glass
            win.toast(backend.glass ? qsTr("Glass on") : qsTr("Glass off"))
        }
    }
    Shortcut {
        sequence: "N"; enabled: !win.typing
        onActivated: {
            const style = backend.cycleTitleStyle()
            win.toast(style === "corner" ? qsTr("Track titles in the corner")
                    : style === "picture" ? qsTr("Track titles in the picture") : qsTr("Track titles off"))
        }
    }
    Shortcut {
        sequence: "M"; enabled: !win.typing
        onActivated: {
            backend.source = backend.source === "microphone" ? "system" : "microphone"
            win.toast(backend.source === "system" ? qsTr("Listening to what's playing") : qsTr("Listening to the microphone"))
        }
    }
    Shortcut {
        sequences: ["+", "="]; enabled: !win.typing
        onActivated: {
            backend.sensitivity = backend.sensitivity * 1.15
            win.toast(qsTr("Sensitivity %1%").arg(Math.round(backend.sensitivity * 100)))
        }
    }
    Shortcut {
        sequence: "-"; enabled: !win.typing
        onActivated: {
            backend.sensitivity = backend.sensitivity / 1.15
            win.toast(qsTr("Sensitivity %1%").arg(Math.round(backend.sensitivity * 100)))
        }
    }
    Shortcut { sequences: [StandardKey.Save]; onActivated: backend.save() }
    Shortcut { sequence: "Ctrl+Shift+S"; onActivated: win.saveAs() }
    Shortcut { sequence: "Ctrl+N"; onActivated: win.newPreset() }
    Shortcut { sequences: [StandardKey.Undo]; enabled: !win.typing; onActivated: backend.undo() }
    Shortcut { sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]; enabled: !win.typing; onActivated: backend.redo() }
    Shortcut {
        // A text box takes Esc itself, and so does a menu, a list or a
        // question that is up: Qt holds the window's shortcuts back while one
        // is, which is why none of these is an application shortcut.
        sequence: "Esc"; enabled: !win.typing
        onActivated: {
            if (win.helpVisible)
                win.helpVisible = false
            else if (win.aboutVisible)
                win.aboutVisible = false
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

    Item {
        id: content
        anchors.fill: parent
        focus: true

        PresetBrowser {
            id: browser
            visible: win.browserVisible
            width: Math.min(300, parent.width * 0.4)
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
            onCloseRequested: win.browserVisible = false
            // Hidden, the search box would keep the keyboard, and every
            // letter key with it, until a click on the picture.
            onVisibleChanged: if (!visible && searching) content.forceActiveFocus()
            onPicked: if (searching) content.forceActiveFocus()
        }

        // This preset's own settings. The panel is only made while it is
        // open: it has a control for every setting of the preset, and kept
        // around out of sight it would rebuild them all at every change of
        // preset, which is a stumble in the picture each time.
        Loader {
            id: editor
            active: win.editorVisible
            visible: active
            width: Math.min(440, parent.width * 0.5)
            anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
            sourceComponent: EditorPanel {
                tab: win.editorTab
                onTabPicked: function (index) { win.editorTab = index }
                onCloseRequested: win.sidePanel = ""
            }
        }

        // The app's settings, the same whatever preset is showing.
        SettingsPanel {
            id: settings
            visible: win.settingsVisible
            width: Math.min(440, parent.width * 0.5)
            anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
            onCloseRequested: win.sidePanel = ""
            onAboutRequested: win.toggleAbout()
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

            // The wordmark, faintly, at the start. It goes when the controls
            // first do, or, if nothing is playing by then, once something has
            // played for a moment, so its bars are seen to take the music up.
            Loader {
                id: startLogo
                anchors.centerIn: parent
                width: Math.min(parent.width * 0.5, 760)
                height: width * 2 / 3 // the logo's 840 by 560
                property bool waited: false
                property bool heard: false
                // Once it starts to go it goes, whatever the sound does next.
                property bool leaving: false
                readonly property bool controlsGone: !pointer.shown && !controls.hovered
                readonly property bool due: waited && heard
                readonly property int fadeOut: 6000
                onControlsGoneChanged: if (controlsGone) waited = true
                onDueChanged: if (due) leaving = true
                Timer { running: !backend.silent; interval: 3000; onTriggered: startLogo.heard = true }
                sourceComponent: Logo {
                    id: shownLogo
                    kick: backend.levels[3] || 0
                    bands: backend.bands
                    opacity: 0
                    NumberAnimation on opacity { to: 0.8; duration: 400 }
                    // Slowly, and easing out of sight rather than stopping
                    // short, as the music takes the picture over.
                    NumberAnimation {
                        target: shownLogo
                        property: "opacity"
                        to: 0
                        duration: startLogo.fadeOut
                        easing.type: Easing.InOutSine
                        running: startLogo.leaving
                    }
                }
                Timer { running: startLogo.leaving; interval: startLogo.fadeOut + 100; onTriggered: startLogo.active = false }
            }

            DebugOverlay {
                id: debug
                anchors { left: parent.left; top: parent.top; margins: 14 }
                visible: backend.debug
                view: picture
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
                               : qsTr("Waiting for sound from %1.").arg(backend.sourceLabel))
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
                onHelp: win.toggleHelp()
                onToggleFullScreen: win.toggleFullScreen()
            }
        }

        HelpOverlay {
            anchors.fill: parent
            visible: win.helpVisible
            onDismissed: win.helpVisible = false
        }

        AboutOverlay {
            anchors.fill: parent
            visible: win.aboutVisible
            view: picture
            onDismissed: win.aboutVisible = false
            onCopied: win.toast(qsTr("Details copied"))
        }
    }

    PopupDialog {
        id: discardDialog
        title: qsTr("Unsaved changes")
        onClosed: content.forceActiveFocus()
        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            font.pixelSize: Ui.body
            color: Ui.textDim
            text: qsTr("%1 has changes that aren't saved.").arg(backend.currentName)
        }
        buttons: [
            TextButton {
                text: qsTr("Save")
                primary: true
                onClicked: {
                    discardDialog.close()
                    if (backend.save() && win.pendingAction)
                        win.pendingAction()
                    win.pendingAction = null
                }
            },
            TextButton {
                text: qsTr("Discard")
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
            },
            TextButton {
                text: qsTr("Cancel")
                onClicked: {
                    win.pendingAction = null
                    discardDialog.close()
                }
            }
        ]
    }

    PopupDialog {
        id: nameDialog
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
        CompactField {
            id: nameField
            width: parent.width
            mono: false
            placeholder: qsTr("Name")
            onAccepted: nameDialog.confirm()
            onCancelled: nameDialog.close()
        }
        buttons: [
            TextButton { text: qsTr("OK"); primary: true; onClicked: nameDialog.confirm() },
            TextButton { text: qsTr("Cancel"); onClicked: nameDialog.close() }
        ]
    }

    PopupDialog {
        id: deleteDialog
        title: qsTr("Delete preset")
        onClosed: content.forceActiveFocus()
        // The preset asked about, which is the one deleted: the presets may
        // move on while the question is up.
        property string name
        property bool shadows
        function ask() {
            name = backend.currentName
            shadows = backend.currentShadowsBuiltin
            open()
        }
        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            font.pixelSize: Ui.body
            color: Ui.textDim
            text: deleteDialog.shadows
                  ? qsTr("Delete your copy of %1? The built-in takes its place again. This can't be undone.").arg(deleteDialog.name)
                  : qsTr("Delete %1? This can't be undone.").arg(deleteDialog.name)
        }
        buttons: [
            TextButton { text: qsTr("Delete"); primary: true; onClicked: { deleteDialog.close(); backend.deletePreset(deleteDialog.name) } },
            TextButton { text: qsTr("Cancel"); onClicked: deleteDialog.close() }
        ]
    }

    // A question with one thing to do if the answer is yes.
    PopupDialog {
        id: confirmDialog
        onClosed: content.forceActiveFocus()
        property string text
        property string button
        property var action
        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            font.pixelSize: Ui.body
            color: Ui.textDim
            text: confirmDialog.text
        }
        buttons: [
            TextButton { text: confirmDialog.button; primary: true; onClicked: { confirmDialog.close(); confirmDialog.action() } },
            TextButton { text: qsTr("Cancel"); onClicked: confirmDialog.close() }
        ]
    }

    // Panels reach the window's helpers through these.
    function askDelete() { deleteDialog.ask() }
    function confirm(title, text, button, action) {
        confirmDialog.title = title
        confirmDialog.text = text
        confirmDialog.button = button
        confirmDialog.action = action
        confirmDialog.open()
    }
    function showHelp(item, title, body, detail) { helpTip.show(item, title, body, detail) }
    function hideHelp(item) { helpTip.hide(item) }

    HelpTip { id: helpTip }
}
