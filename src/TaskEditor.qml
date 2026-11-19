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
    title: taskId ? qsTr("Edit task") : qsTr("New task")
    Material.roundedScale: theme.radius

    property int taskId: 0
    property var months: []

    function openFor(task) {
        taskId = task.id || 0
        titleField.text = task.title || ""
        notesField.text = task.notes || ""
        const contextIndex = contextBox.model.findIndex(c => c.id === (task.context || 0))
        contextBox.currentIndex = Math.max(0, contextIndex)
        priorityBox.currentIndex = (task.priority || 2) - 1
        const kind = task.scheduleKind || "every"
        kindBox.currentIndex = ["every", "ask", "once"].indexOf(kind)
        everyBox.value = task.every || 1
        unitBox.currentIndex = Math.max(0, ["day", "week", "month", "year"].indexOf(task.unit || "week"))
        fromDoneBox.currentIndex = task.fromDone === false ? 1 : 0
        months = (task.months || []).slice()
        allYear.checked = months.length === 0
        due.date = task.due || backend.dateAfter(0, "day")
        open()
        titleField.forceActiveFocus()
    }

    function save() {
        if (titleField.text.trim() === "")
            return
        backend.saveTask({
            id: taskId,
            title: titleField.text,
            notes: notesField.text,
            context: contextBox.model[contextBox.currentIndex].id,
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

            TextField {
                id: titleField
                Layout.columnSpan: 2
                Layout.fillWidth: true
                placeholderText: qsTr("What needs doing")
                onAccepted: editor.save()
            }

            Label { text: qsTr("Context"); color: "#a0a0a8" }
            ComboBox {
                id: contextBox
                Layout.fillWidth: true
                textRole: "label"
                model: [{ id: 0, label: qsTr("No context") }].concat(
                    backend.contexts.map(c => ({ id: c.id, label: "    ".repeat(c.depth) + c.name })))
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
                text: editor.taskId ? qsTr("Next due") : qsTr("First due")
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
                placeholderText: qsTr("Notes: part numbers, how-tos, where the filter lives")
                wrapMode: TextEdit.Wrap
            }
        }
    }

    footer: DialogButtonBox {
        Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        Button {
            text: editor.taskId ? qsTr("Save") : qsTr("Add")
            highlighted: true
            enabled: titleField.text.trim() !== ""
            onClicked: editor.save()
        }
    }
}
