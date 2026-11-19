import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1180
    height: 760
    minimumWidth: 760
    minimumHeight: 480
    visible: true
    title: "Needful"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    property int selectedId: 0
    readonly property var selected: {
        // Re-read whenever the lists change, so the detail pane stays current.
        backend.items
        translator.language
        return selectedId && backend.view !== "history" ? backend.task(selectedId) : ({})
    }
    readonly property bool hasSelection: selected.id !== undefined
    // Single-key shortcuts stand down while a dialog is open.
    readonly property bool dialogOpen: helpVisible || editor.visible || completeDialog.visible
                                       || snoozeDialog.visible || contextDialog.visible || confirm.visible
                                       || logDialog.visible

    readonly property var views: [
        { key: "today", label: qsTr("Today") },
        { key: "needful", label: qsTr("Needful") },
        { key: "upcoming", label: qsTr("Upcoming") },
        { key: "history", label: qsTr("History") }
    ]

    function viewTitle() {
        for (const v of views)
            if (v.key === backend.view)
                return v.label
        return ""
    }

    function selectIndex(i) {
        const items = backend.items
        if (items.length === 0) {
            list.currentIndex = -1
            selectedId = 0
            return
        }
        i = Math.max(0, Math.min(items.length - 1, i))
        list.currentIndex = i
        selectedId = items[i].kind === "task" ? items[i].id : 0
        list.positionViewAtIndex(i, ListView.Contain)
    }

    // After any change, keep the same task selected if it's still listed,
    // or the row that took its place.
    Connections {
        target: backend
        function onChanged() {
            const items = backend.items
            for (let i = 0; i < items.length; ++i)
                if (items[i].kind === "task" && items[i].id === win.selectedId) {
                    list.currentIndex = i
                    return
                }
            win.selectIndex(Math.min(list.currentIndex, items.length - 1))
        }
        function onViewChanged() { win.selectIndex(0) }
    }
    Component.onCompleted: selectIndex(0)

    function cycleContext(step) {
        const ids = [0].concat(backend.contexts.map(c => c.id))
        const at = Math.max(0, ids.indexOf(backend.contextFilter))
        backend.contextFilter = ids[(at + step + ids.length) % ids.length]
    }

    function newTask() { editor.openFor({ context: backend.contextFilter }) }
    function editSelected() { if (hasSelection) editor.openFor(selected) }
    function completeSelected() { if (hasSelection) completeDialog.openFor(selected) }
    function snoozeSelected() { if (hasSelection) snoozeDialog.openFor(selected) }
    function todaySelected() { if (hasSelection) backend.toggleToday(selectedId) }
    function deleteSelected() {
        if (backend.view === "history" && list.currentIndex >= 0) {
            const entry = backend.items[list.currentIndex]
            if (entry.kind === "log")
                confirm.ask(qsTr("Remove this note? Its follow-up task, if any, stays."),
                            qsTr("Remove"), () => backend.removeLog(entry.id))
            else
                confirm.ask(qsTr("Remove this entry from the history of “%1”?").arg(entry.title),
                            qsTr("Remove"), () => backend.removeCompletion(entry.id))
        } else if (hasSelection) {
            const id = selectedId
            confirm.ask(qsTr("Delete “%1”? What you've done stays in the history.").arg(selected.title),
                        qsTr("Delete"), () => backend.removeTask(id))
        }
    }

    Shortcut { sequences: ["?", "Shift+?"]; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; enabled: !win.dialogOpen; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequences: [StandardKey.Quit, "Ctrl+Q"]; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "Space"; enabled: !win.dialogOpen; context: Qt.ApplicationShortcut; onActivated: win.completeSelected() }
    Shortcut { sequence: "T"; enabled: !win.dialogOpen; context: Qt.ApplicationShortcut; onActivated: win.todaySelected() }
    Shortcut { sequence: "S"; enabled: !win.dialogOpen; context: Qt.ApplicationShortcut; onActivated: win.snoozeSelected() }
    Shortcut { sequences: ["N", StandardKey.New]; enabled: !win.dialogOpen; context: Qt.ApplicationShortcut; onActivated: win.newTask() }
    Shortcut { sequences: ["E", "Return", "Enter"]; enabled: !win.dialogOpen && !search.activeFocus; context: Qt.ApplicationShortcut; onActivated: win.editSelected() }
    Shortcut { sequences: ["Delete", "X"]; enabled: !win.dialogOpen; context: Qt.ApplicationShortcut; onActivated: win.deleteSelected() }
    Shortcut { sequences: ["J", "Down"]; enabled: !win.dialogOpen; context: Qt.ApplicationShortcut; onActivated: win.selectIndex(list.currentIndex + 1) }
    Shortcut { sequences: ["K", "Up"]; enabled: !win.dialogOpen; context: Qt.ApplicationShortcut; onActivated: win.selectIndex(list.currentIndex - 1) }
    Shortcut { sequence: "1"; enabled: !win.dialogOpen; context: Qt.ApplicationShortcut; onActivated: backend.view = "today" }
    Shortcut { sequence: "2"; enabled: !win.dialogOpen; context: Qt.ApplicationShortcut; onActivated: backend.view = "needful" }
    Shortcut { sequence: "3"; enabled: !win.dialogOpen; context: Qt.ApplicationShortcut; onActivated: backend.view = "upcoming" }
    Shortcut { sequence: "4"; enabled: !win.dialogOpen; context: Qt.ApplicationShortcut; onActivated: backend.view = "history" }
    Shortcut { sequence: "]"; enabled: !win.dialogOpen; context: Qt.ApplicationShortcut; onActivated: win.cycleContext(1) }
    Shortcut { sequence: "["; enabled: !win.dialogOpen; context: Qt.ApplicationShortcut; onActivated: win.cycleContext(-1) }
    Shortcut { sequence: "0"; enabled: !win.dialogOpen; context: Qt.ApplicationShortcut; onActivated: backend.contextFilter = 0 }
    Shortcut { sequence: "L"; enabled: !win.dialogOpen; context: Qt.ApplicationShortcut; onActivated: logDialog.openFor(backend.contextFilter) }
    Shortcut { sequence: "C"; enabled: !win.dialogOpen; context: Qt.ApplicationShortcut; onActivated: contextDialog.openForNew(backend.contextFilter) }
    Shortcut { sequence: "Shift+C"; enabled: !win.dialogOpen; context: Qt.ApplicationShortcut; onActivated: contextDialog.openForNew(0) }
    Shortcut { sequences: ["/", StandardKey.Find]; enabled: !win.dialogOpen; context: Qt.ApplicationShortcut; onActivated: search.forceActiveFocus() }
    Shortcut { sequences: [StandardKey.Undo]; enabled: !win.dialogOpen; context: Qt.ApplicationShortcut; onActivated: backend.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Shift+Z"]; enabled: !win.dialogOpen; context: Qt.ApplicationShortcut; onActivated: backend.redo() }
    Shortcut {
        sequence: "Escape"
        enabled: !win.dialogOpen && (search.activeFocus || backend.search !== "")
        context: Qt.ApplicationShortcut
        onActivated: { search.text = ""; list.forceActiveFocus() }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        RowLayout {
            anchors.fill: parent
            spacing: 0

            Sidebar {
                Layout.preferredWidth: win.width < 1000 ? 200 : 236
                Layout.fillHeight: true
                views: win.views
                onAddContext: parent => contextDialog.openForNew(parent)
                onRenameContext: c => contextDialog.openForRename(c)
                onRemoveContext: c => confirm.ask(
                    qsTr("Delete the context “%1”? Its tasks and history move up to its parent.").arg(c.name),
                    qsTr("Delete"), () => backend.removeContext(c.id))
            }

            Rectangle { Layout.fillHeight: true; width: 1; color: "#222228" }

            // The list.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 20
                    Layout.bottomMargin: 8
                    spacing: 16

                    ColumnLayout {
                        spacing: 2
                        Layout.fillWidth: true
                        Layout.minimumWidth: 100
                        Label {
                            text: win.viewTitle()
                            font.pixelSize: 26
                            font.bold: true
                            color: "#f2f2f4"
                        }
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            color: "#8a8a93"
                            text: {
                                const c = backend.contexts.find(c => c.id === backend.contextFilter)
                                const where = c ? c.path : qsTr("Everything")
                                return backend.view === "today" ? backend.todayText + "  ·  " + where : where
                            }
                        }
                    }

                    TextField {
                        id: search
                        Layout.fillWidth: true
                        Layout.minimumWidth: 110
                        Layout.maximumWidth: 260
                        placeholderText: qsTr("Search  /")
                        text: backend.search
                        onTextEdited: backend.search = text
                        Keys.onDownPressed: list.forceActiveFocus()
                        Keys.onReturnPressed: list.forceActiveFocus()
                    }

                    Button {
                        text: qsTr("Log")
                        flat: true
                        onClicked: logDialog.openFor(backend.contextFilter)
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("Note something on a context (L)")
                    }

                    Button {
                        text: qsTr("New task")
                        highlighted: true
                        onClicked: win.newTask()
                    }
                }

                ListView {
                    id: list
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    focus: true
                    model: backend.items
                    spacing: 2
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    topMargin: 4
                    bottomMargin: 20
                    highlightFollowsCurrentItem: false

                    delegate: Item {
                        id: row
                        required property var modelData
                        required property int index
                        width: list.width
                        height: loader.height

                        Loader {
                            id: loader
                            x: 12
                            width: parent.width - 24
                            sourceComponent: row.modelData.kind === "task" ? taskRow
                                           : row.modelData.kind === "log" ? logRow : completionRow
                            Component {
                                id: taskRow
                                TaskRow {
                                    task: row.modelData
                                    current: row.index === list.currentIndex
                                    onPicked: win.selectIndex(row.index)
                                    onCompleteRequested: { win.selectIndex(row.index); win.completeSelected() }
                                    onTodayRequested: backend.toggleToday(row.modelData.id)
                                    onSnoozeRequested: { win.selectIndex(row.index); win.snoozeSelected() }
                                    onEditRequested: { win.selectIndex(row.index); win.editSelected() }
                                }
                            }
                            Component {
                                id: logRow
                                LogRow {
                                    entry: row.modelData
                                    current: row.index === list.currentIndex
                                    onPicked: win.selectIndex(row.index)
                                }
                            }
                            Component {
                                id: completionRow
                                CompletionRow {
                                    entry: row.modelData
                                    current: row.index === list.currentIndex
                                    onPicked: win.selectIndex(row.index)
                                }
                            }
                        }
                    }

                    // Today's list ends with what's already done, so a clean
                    // list reads as finished rather than empty.
                    footer: Item {
                        width: list.width
                        height: backend.view === "today" && backend.doneToday.length > 0 ? doneColumn.height + 24 : 0
                        visible: height > 0
                        Column {
                            id: doneColumn
                            x: 12
                            y: 20
                            width: parent.width - 24
                            spacing: 2
                            Label {
                                text: qsTr("Done today")
                                color: "#8a8a93"
                                font.bold: true
                                bottomPadding: 6
                                leftPadding: 12
                            }
                            Repeater {
                                model: backend.view === "today" ? backend.doneToday : []
                                CompletionRow {
                                    required property var modelData
                                    width: doneColumn.width
                                    entry: modelData
                                    compact: true
                                }
                            }
                        }
                    }

                    Label {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 48, 420)
                        visible: backend.items.length === 0
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        color: "#6e6e78"
                        font.pixelSize: 15
                        text: backend.search !== "" ? qsTr("Nothing matches “%1”.").arg(backend.search)
                            : backend.view === "today" ? (backend.counts.doneToday > 0
                                ? "" : qsTr("Nothing pulled into today yet. Pick from Needful with T."))
                            : backend.view === "needful" ? qsTr("Nothing needs doing. Enjoy it.")
                            : backend.view === "upcoming" ? qsTr("Nothing coming up. Add a task with N.")
                            : qsTr("Nothing done here yet. What you finish shows up here, notes and all.")
                    }
                }
            }

            Rectangle { Layout.fillHeight: true; width: 1; color: "#222228"; visible: detail.visible }

            TaskDetail {
                id: detail
                Layout.preferredWidth: win.width < 1100 ? 300 : 360
                Layout.fillHeight: true
                // A narrow window gives the list the room; Enter still edits.
                visible: win.hasSelection && win.width >= 860
                task: win.selected
                onCompleteRequested: win.completeSelected()
                onTodayRequested: win.todaySelected()
                onSnoozeRequested: win.snoozeSelected()
                onEditRequested: win.editSelected()
                onDeleteRequested: win.deleteSelected()
                onRemoveCompletion: id => confirm.ask(qsTr("Remove this entry from the history?"),
                                                     qsTr("Remove"), () => backend.removeCompletion(id))
            }
        }

        // Saving failed, or the data file can't be read: say so, in the window.
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottomMargin: 20
            visible: backend.error !== ""
            width: Math.min(parent.width - 40, errorRow.implicitWidth + 32)
            height: errorRow.implicitHeight + 20
            radius: theme.radius
            color: "#3a1418"
            border.color: "#7a2a31"
            RowLayout {
                id: errorRow
                anchors.centerIn: parent
                width: parent.width - 32
                Label { text: backend.error; color: "#ffd9dc"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Button { text: qsTr("Dismiss"); flat: true; onClicked: backend.dismissError() }
            }
        }

        HelpOverlay {
            anchors.fill: parent
            visible: win.helpVisible
            onDismissed: win.helpVisible = false
        }
    }

    TaskEditor { id: editor }
    CompleteDialog { id: completeDialog }
    LogDialog { id: logDialog }
    SnoozeDialog { id: snoozeDialog }
    ContextDialog { id: contextDialog }

    Dialog {
        id: confirm
        property var action: null
        property alias message: confirmText.text
        anchors.centerIn: parent
        modal: true
        width: Math.min(win.width - 48, 440)
        Material.roundedScale: theme.radius
        function ask(text, verb, then) {
            message = text
            acceptButton.text = verb
            action = then
            open()
            acceptButton.forceActiveFocus()
        }
        Label { id: confirmText; width: parent.width; wrapMode: Text.WordWrap }
        footer: DialogButtonBox {
            Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Button { id: acceptButton; highlighted: true; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
        }
        onAccepted: { if (action) action(); action = null }
    }
}
