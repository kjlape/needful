import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Everything about the selected task, and every time it was done.
Rectangle {
    id: pane
    property var task: ({})
    signal completeRequested()
    signal todayRequested()
    signal snoozeRequested()
    signal skipRequested()
    signal editRequested()
    signal deleteRequested()
    signal removeCompletion(int id)
    signal removeNote(int id)
    signal noteOnTime(var entry) // Add a note about a past time.
    signal stepsLeft() // Escape from adding a step, or a note.

    // A, from the window: start typing a step for this time.
    function addStep() { stepField.forceActiveFocus() }

    component NoteLine: RowLayout {
        id: noteLine
        property var note: ({})
        property bool removable: false
        Layout.fillWidth: true
        spacing: 8
        Label { text: noteLine.note.date || ""; color: "#85858e"; font.pixelSize: 12; Layout.alignment: Qt.AlignTop }
        Label {
            textFormat: Text.PlainText
            text: noteLine.note.text || ""
            wrapMode: Text.Wrap
            color: "#c4c4cc"
            font.pixelSize: 13
            Layout.fillWidth: true
        }
        ToolButton {
            visible: noteLine.removable
            text: "×"
            implicitWidth: 28
            implicitHeight: 28
            Layout.alignment: Qt.AlignTop
            onClicked: pane.removeNote(noteLine.note.id)
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Remove this note")
        }
    }

    function doneStepsText(steps, skipped) {
        return (steps || []).map(s => (s.checked ? "✓ " : "– ") + s.text).join("   ")
    }

    color: "#121215"
    Material.roundedScale: theme.radius

    readonly property var priorityNames: ["", qsTr("Low priority"), qsTr("Normal priority"), qsTr("High priority"), qsTr("Urgent")]

    component Chip: Rectangle {
        property alias text: chipLabel.text
        property color tint: "#c4c4cc"
        radius: Math.min(4, theme.radius)
        color: Qt.rgba(1, 1, 1, 0.06)
        implicitWidth: chipLabel.implicitWidth + 16
        implicitHeight: 24
        visible: text !== ""
        Label { id: chipLabel; anchors.centerIn: parent; font.pixelSize: 12; color: parent.tint }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: column.implicitHeight + 40
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: column
            x: 20
            y: 20
            width: pane.width - 40
            spacing: 14

            Label {
                text: pane.task.contextPath || qsTr("No context")
                color: "#85858e"
                font.pixelSize: 12
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Label {
                text: pane.task.title || ""
                font.pixelSize: 21
                font.bold: true
                wrapMode: Text.Wrap
                color: "#f2f2f4"
                Layout.fillWidth: true
                Layout.topMargin: -8
            }

            Flow {
                Layout.fillWidth: true
                spacing: 6
                Chip { text: pane.task.dueText || ""; tint: (pane.task.overdue || 0) > 0 ? theme.accent : "#c4c4cc" }
                Chip { text: pane.priorityNames[pane.task.priority || 2] }
                Chip { text: pane.task.scheduleText || "" }
                Chip { text: pane.task.seasonText || "" }
                Chip { text: pane.task.today ? qsTr("On today's list") : ""; tint: theme.accent }
                Chip {
                    readonly property var own: pane.task.overrides || []
                    text: own.length === 0 ? "" : qsTr("Own %1").arg(own.map(f => f === "schedule" ? qsTr("schedule")
                                                                            : f === "priority" ? qsTr("priority")
                                                                            : qsTr("season")).join(", "))
                }
            }

            // A routine's shared notes, then this context's own.
            Label {
                visible: text !== ""
                text: pane.task.sharedNotes || ""
                wrapMode: Text.Wrap
                color: "#c4c4cc"
                Layout.fillWidth: true
            }
            Label {
                visible: text !== ""
                text: pane.task.notes || ""
                wrapMode: Text.Wrap
                color: "#c4c4cc"
                Layout.fillWidth: true
            }

            // The checklist: tick by click, or the next one with Shift+Space.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Repeater {
                    model: pane.task.steps || []
                    RowLayout {
                        id: stepRow
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 0
                        CheckBox {
                            Layout.fillWidth: true
                            checked: stepRow.modelData.checked
                            text: stepRow.modelData.text
                            onToggled: backend.checkStep(pane.task.id, stepRow.modelData.id, checked)
                            contentItem: Label {
                                leftPadding: parent.indicator.width + parent.spacing
                                text: parent.text
                                wrapMode: Text.Wrap
                                verticalAlignment: Text.AlignVCenter
                                color: parent.checked ? "#85858e" : "#ececf0"
                                font.strikeout: parent.checked
                            }
                        }
                        Label {
                            visible: stepRow.modelData.once
                            text: qsTr("this time")
                            font.pixelSize: 11
                            color: "#6e6e78"
                        }
                        ToolButton {
                            visible: stepRow.modelData.once
                            text: "×"
                            implicitWidth: 28
                            implicitHeight: 28
                            onClicked: backend.removeStep(pane.task.id, stepRow.modelData.id)
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Remove this step")
                        }
                    }
                }
                TextField {
                    id: stepField
                    Layout.fillWidth: true
                    placeholderText: qsTr("+ Add a step for this time (A)")
                    font.pixelSize: 13
                    leftPadding: 10
                    topPadding: 8
                    bottomPadding: 8
                    background: Rectangle {
                        radius: Math.min(6, theme.radius)
                        color: stepField.activeFocus ? Qt.rgba(1, 1, 1, 0.06) : "transparent"
                        border.color: stepField.activeFocus ? theme.accent : Qt.rgba(1, 1, 1, 0.08)
                    }
                    // Enter adds it and stays here for the next one.
                    onAccepted: {
                        if (text.trim() !== "" && backend.addStep(pane.task.id, text) > 0)
                            text = ""
                    }
                    Keys.onEscapePressed: { text = ""; pane.stepsLeft() }
                }
            }

            // What's been noticed this time, kept until it's done or skipped,
            // when it goes into the history with it.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label { text: qsTr("Notes this time"); color: "#85858e"; font.pixelSize: 12 }
                Repeater {
                    model: pane.task.notesThisTime || []
                    NoteLine { required property var modelData; note: modelData; removable: true }
                }
                TextField {
                    id: noteField
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    placeholderText: qsTr("+ Add a note (Shift+L)")
                    font.pixelSize: 13
                    leftPadding: 10
                    topPadding: 8
                    bottomPadding: 8
                    background: Rectangle {
                        radius: Math.min(6, theme.radius)
                        color: noteField.activeFocus ? Qt.rgba(1, 1, 1, 0.06) : "transparent"
                        border.color: noteField.activeFocus ? theme.accent : Qt.rgba(1, 1, 1, 0.08)
                    }
                    // Enter adds it and stays here for the next one.
                    onAccepted: {
                        if (text.trim() !== "" && backend.addLog({ about: pane.task.id, text: text }) > 0)
                            text = ""
                    }
                    Keys.onEscapePressed: { text = ""; pane.stepsLeft() }
                }
            }

            ColumnLayout {
                visible: (pane.task.copies || []).length > 0
                Layout.fillWidth: true
                spacing: 4
                Label { text: qsTr("Also in"); color: "#85858e"; font.pixelSize: 12 }
                Repeater {
                    model: pane.task.copies || []
                    RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Label { text: modelData.contextPath; color: "#c4c4cc"; elide: Text.ElideRight; Layout.fillWidth: true }
                        Label { text: modelData.dueText; color: "#85858e"; font.pixelSize: 12 }
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 6
                rowSpacing: 0
                Button {
                    Layout.fillWidth: true
                    Layout.columnSpan: 2
                    highlighted: true
                    text: qsTr("Done")
                    onClicked: pane.completeRequested()
                }
                Button {
                    Layout.fillWidth: true
                    flat: true
                    text: pane.task.today ? qsTr("Off today") : qsTr("Today")
                    onClicked: pane.todayRequested()
                }
                Button {
                    Layout.fillWidth: true
                    flat: true
                    text: pane.task.state === "snoozed" ? qsTr("Wake up") : qsTr("Snooze")
                    onClicked: pane.task.state === "snoozed" ? backend.unsnooze(pane.task.id) : pane.snoozeRequested()
                }
                Button {
                    Layout.fillWidth: true
                    Layout.columnSpan: 2
                    flat: true
                    text: qsTr("Skip this time")
                    onClicked: pane.skipRequested()
                }
                Button { Layout.fillWidth: true; flat: true; text: qsTr("Edit"); onClicked: pane.editRequested() }
                Button { Layout.fillWidth: true; flat: true; text: qsTr("Delete"); onClicked: pane.deleteRequested() }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#222228" }

            Label {
                text: qsTr("History")
                font.bold: true
                color: "#c4c4cc"
            }
            Label {
                visible: (pane.task.history || []).length === 0
                text: qsTr("Not done yet. Notes you leave when you finish it show up here.")
                wrapMode: Text.Wrap
                color: "#6e6e78"
                Layout.fillWidth: true
            }
            Repeater {
                model: pane.task.history || []
                ColumnLayout {
                    id: entry
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: entry.modelData.date; color: "#ececf0"; font.bold: true }
                        Label {
                            text: entry.modelData.skipped ? qsTr("%1 · skipped").arg(entry.modelData.ago) : entry.modelData.ago
                            color: "#85858e"
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        ToolButton {
                            text: "×"
                            implicitWidth: 28
                            implicitHeight: 28
                            opacity: entryHover.hovered || hovered ? 1 : 0
                            onClicked: pane.removeCompletion(entry.modelData.id)
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Remove from history")
                        }
                    }
                    Label {
                        visible: text !== ""
                        text: pane.doneStepsText(entry.modelData.steps)
                        wrapMode: Text.Wrap
                        color: "#85858e"
                        font.pixelSize: 12
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: entry.modelData.note !== ""
                        text: entry.modelData.note
                        wrapMode: Text.Wrap
                        color: "#c4c4cc"
                        Layout.fillWidth: true
                    }
                    // The notes about that time, oldest first.
                    Repeater {
                        model: entry.modelData.notes || []
                        NoteLine { required property var modelData; note: modelData; removable: true }
                    }
                    Label {
                        text: qsTr("+ Add a note")
                        color: noteHover.hovered ? theme.accent : "#6e6e78"
                        font.pixelSize: 12
                        opacity: entryHover.hovered || noteHover.hovered ? 1 : 0.6
                        HoverHandler { id: noteHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: pane.noteOnTime(entry.modelData) }
                    }
                    HoverHandler { id: entryHover }
                }
            }
        }
    }
}
