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
    signal editRequested()
    signal deleteRequested()
    signal removeCompletion(int id)

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
                        Label { text: entry.modelData.ago; color: "#85858e"; Layout.fillWidth: true; elide: Text.ElideRight }
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
                        visible: entry.modelData.note !== ""
                        text: entry.modelData.note
                        wrapMode: Text.Wrap
                        color: "#c4c4cc"
                        Layout.fillWidth: true
                    }
                    HoverHandler { id: entryHover }
                }
            }
        }
    }
}
