import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Adds a task, or changes one.
Dialog {
    id: editor
    anchors.centerIn: parent
    modal: true
    width: Math.min(parent.width - 48, 580)
    height: Math.min(parent.height - 48, implicitHeight)
    title: isNew ? qsTr("New task") : isCopy ? qsTr("Edit routine") : qsTr("Edit task")
    Material.roundedScale: theme.radius

    property int taskId: 0
    property var task: ({})
    property var months: []
    property var ticked: []      // Context ids with a copy, or the one context.
    property string scope: "all" // For a routine's copy: "all" or "only".
    readonly property bool isCopy: (task.routine || 0) > 0
    readonly property bool onlyThis: isCopy && scope === "only"
    readonly property bool isNew: taskId === 0
    // Just the innermost name: "Tacoma", not "Automotive › Tacoma".
    readonly property string contextName: (task.contextPath || "").split(" › ").slice(-1)[0]

    // Fills the fields from a task, or from its routine's shared values.
    function load(source) {
        notesField.text = source.notes || ""
        priorityBox.currentIndex = (source.priority || 2) - 1
        kindBox.currentIndex = Math.max(0, ["every", "ask", "once"].indexOf(source.scheduleKind || "every"))
        everyBox.value = source.every || 1
        unitBox.currentIndex = Math.max(0, ["day", "week", "month", "year"].indexOf(source.unit || "week"))
        fromDoneBox.currentIndex = source.fromDone === false ? 1 : 0
        months = (source.months || []).slice()
        allYear.checked = months.length === 0
    }

    function setScope(value) {
        scope = value
        load(onlyThis ? task : task.shared)
        // A copy's own due date is edited only here; new contexts start on the chosen day.
        due.date = onlyThis ? task.due : backend.dateAfter(0, "day")
    }

    function openFor(source) {
        task = source.id ? backend.task(source.id) : source
        taskId = task.id || 0
        titleField.text = task.title || ""
        ticked = isCopy ? (task.contexts || []).slice() : (task.context ? [task.context] : [])
        if (isCopy) {
            setScope("all")
        } else {
            scope = "all"
            load(task)
            due.date = task.due || backend.dateAfter(0, "day")
        }
        open()
        titleField.forceActiveFocus()
    }

    function toggleContext(id) {
        const list = ticked.slice()
        const at = list.indexOf(id)
        if (at >= 0) list.splice(at, 1); else list.push(id)
        ticked = list
    }

    function save() {
        if (titleField.text.trim() === "" || (isCopy && !onlyThis && ticked.length === 0))
            return
        backend.saveTask({
            id: taskId,
            scope: scope,
            title: titleField.text,
            notes: notesField.text,
            contexts: ticked,
            priority: priorityBox.currentIndex + 1,
            scheduleKind: ["every", "ask", "once"][kindBox.currentIndex],
            every: everyBox.value,
            unit: ["day", "week", "month", "year"][unitBox.currentIndex],
            fromDone: fromDoneBox.currentIndex === 0,
            months: allYear.checked ? [] : months,
            due: due.date
        })
        close()
    }

    Shortcut { sequences: ["Ctrl+Return", "Ctrl+Enter", StandardKey.Save]; enabled: editor.visible; onActivated: editor.save() }

    contentItem: Flickable {
        implicitHeight: form.implicitHeight
        contentHeight: form.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        GridLayout {
            id: form
            width: parent.width
            columns: 2
            columnSpacing: 16
            rowSpacing: 10

            // A routine's copy is edited for every context, or for this one alone.
            RowLayout {
                visible: editor.isCopy
                Layout.columnSpan: 2
                Layout.fillWidth: true
                spacing: 4
                Button {
                    text: qsTr("All %n contexts", "", editor.ticked.length)
                    checkable: true
                    checked: !editor.onlyThis
                    flat: !checked
                    highlighted: checked
                    onClicked: editor.setScope("all")
                }
                Button {
                    text: qsTr("Only %1").arg(editor.contextName)
                    checkable: true
                    checked: editor.onlyThis
                    flat: !checked
                    highlighted: checked
                    onClicked: editor.setScope("only")
                    Layout.fillWidth: true
                    Layout.maximumWidth: implicitWidth
                    contentItem: Label {
                        text: parent.text
                        elide: Text.ElideMiddle
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: parent.checked ? theme.accentForeground : "#ececf0"
                        font: parent.font
                    }
                }
            }

            TextField {
                id: titleField
                visible: !editor.onlyThis
                Layout.columnSpan: 2
                Layout.fillWidth: true
                placeholderText: qsTr("What needs doing")
                onAccepted: editor.save()
            }

            Label {
                visible: editor.onlyThis
                Layout.columnSpan: 2
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: "#a0a0a8"
                text: qsTr("Changes here apply to %1 alone. Anything set differently from the routine stays that way when the routine changes.").arg(editor.contextName)
            }

            Label {
                visible: !editor.onlyThis
                text: qsTr("Contexts")
                color: "#a0a0a8"
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 10
            }
            ColumnLayout {
                visible: !editor.onlyThis
                Layout.fillWidth: true
                spacing: 0
                Label {
                    visible: backend.contexts.length === 0
                    text: qsTr("No contexts yet. Add one with C.")
                    color: "#6e6e78"
                    Layout.topMargin: 10
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 2
                    Repeater {
                        model: backend.contexts
                        Button {
                            required property var modelData
                            text: modelData.depth > 0 ? modelData.path.split(" › ").slice(-1)[0] : modelData.name
                            checkable: true
                            checked: editor.ticked.indexOf(modelData.id) >= 0
                            flat: !checked
                            highlighted: checked
                            font.pixelSize: 12
                            onClicked: editor.toggleContext(modelData.id)
                            ToolTip.visible: hovered && modelData.depth > 0
                            ToolTip.delay: 500
                            ToolTip.text: modelData.path
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: 12
                    color: editor.isCopy && editor.ticked.length === 0 ? "#ff7a7a" : "#85858e"
                    text: editor.isCopy && editor.ticked.length === 0
                          ? qsTr("Tick at least one context, or delete the routine instead.")
                          : editor.ticked.length > 1
                          ? qsTr("Each context gets its own copy, with its own due date and history.")
                          : editor.ticked.length === 0 ? qsTr("No context") : ""
                    visible: text !== ""
                }
            }

            Label { text: qsTr("Priority"); color: "#a0a0a8" }
            ComboBox {
                id: priorityBox
                Layout.fillWidth: true
                model: [qsTr("Low"), qsTr("Normal"), qsTr("High"), qsTr("Urgent")]
            }

            Label { text: qsTr("Repeats"); color: "#a0a0a8" }
            ComboBox {
                id: kindBox
                Layout.fillWidth: true
                model: [qsTr("On a schedule"), qsTr("Ask me when I finish"), qsTr("Just once")]
            }

            Item { width: 1; height: 1; visible: kindBox.currentIndex === 0 }
            RowLayout {
                visible: kindBox.currentIndex === 0
                Layout.fillWidth: true
                spacing: 8
                Label { text: qsTr("Every") }
                SpinBox { id: everyBox; from: 1; to: 999; editable: true; Layout.preferredWidth: 130 }
                ComboBox {
                    id: unitBox
                    Layout.fillWidth: true
                    model: [qsTr("days", "", everyBox.value), qsTr("weeks", "", everyBox.value),
                            qsTr("months", "", everyBox.value), qsTr("years", "", everyBox.value)]
                }
            }

            Item { width: 1; height: 1; visible: kindBox.currentIndex === 0 }
            ComboBox {
                id: fromDoneBox
                visible: kindBox.currentIndex === 0
                Layout.fillWidth: true
                model: [qsTr("Counting from when it's done"), qsTr("On a fixed rhythm, however late")]
            }

            Label { text: qsTr("In season"); color: "#a0a0a8"; Layout.alignment: Qt.AlignTop; Layout.topMargin: 12 }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                CheckBox {
                    id: allYear
                    text: qsTr("All year")
                    onToggled: if (!checked && editor.months.length === 0) editor.months = [4, 5, 6, 7, 8, 9, 10]
                }
                GridLayout {
                    visible: !allYear.checked
                    columns: 6
                    columnSpacing: 2
                    rowSpacing: 2
                    Repeater {
                        model: 12
                        Button {
                            required property int index
                            readonly property int month: index + 1
                            text: { translator.language; return Qt.locale().standaloneMonthName(index, Locale.ShortFormat) }
                            checkable: true
                            flat: !checked
                            checked: editor.months.indexOf(month) >= 0
                            highlighted: checked
                            font.pixelSize: 12
                            Layout.fillWidth: true
                            onClicked: {
                                const m = editor.months.slice()
                                const at = m.indexOf(month)
                                if (at >= 0) m.splice(at, 1); else m.push(month)
                                editor.months = m
                            }
                        }
                    }
                }
            }

            Label {
                text: editor.isCopy && !editor.onlyThis ? qsTr("First due for newly ticked contexts")
                    : editor.isNew ? qsTr("First due") : qsTr("Next due")
                wrapMode: Text.Wrap
                Layout.maximumWidth: 120
                color: "#a0a0a8"
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 10
            }
            DateStepper { id: due; Layout.fillWidth: true }

            TextArea {
                id: notesField
                Layout.columnSpan: 2
                Layout.fillWidth: true
                Layout.minimumHeight: 70
                placeholderText: editor.onlyThis ? qsTr("Notes for %1 only").arg(editor.contextName)
                               : editor.isCopy || editor.ticked.length > 1 ? qsTr("Notes shared by every context")
                               : qsTr("Notes: part numbers, how-tos, where the filter lives")
                wrapMode: TextEdit.Wrap
            }
        }
    }

    footer: DialogButtonBox {
        Button {
            visible: editor.onlyThis
            text: qsTr("Reset to shared")
            flat: true
            DialogButtonBox.buttonRole: DialogButtonBox.ResetRole
            onClicked: { const notes = notesField.text; editor.load(editor.task.shared); notesField.text = notes }
        }
        Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        Button {
            text: editor.taskId ? qsTr("Save") : qsTr("Add")
            highlighted: true
            enabled: titleField.text.trim() !== ""
            onClicked: editor.save()
        }
    }
}
