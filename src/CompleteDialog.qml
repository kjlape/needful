import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Marks a task done, with a note for next time, and for tasks that ask,
// when it should come back.
Dialog {
    id: dialog
    anchors.centerIn: parent
    modal: true
    width: Math.min(parent.width - 48, 520)
    title: qsTr("Done: %1").arg(task.title || "")
    Material.roundedScale: theme.radius

    property var task: ({})
    readonly property bool asks: task.scheduleKind === "ask"

    function openFor(t) {
        task = t
        note.text = ""
        notAgain.checked = false
        next.date = backend.suggestedNext(t.id)
        open()
        note.forceActiveFocus()
    }

    function finish() {
        backend.complete(task.id, note.text, asks && !notAgain.checked ? next.date : "")
        close()
    }

    ColumnLayout {
        width: parent.width
        spacing: 12

        TextArea {
            id: note
            Layout.fillWidth: true
            Layout.minimumHeight: 90
            wrapMode: TextEdit.Wrap
            placeholderText: qsTr("Note for next time (optional)")
            // Enter finishes; Shift+Enter starts a new line.
            Keys.onReturnPressed: event => {
                if (event.modifiers & Qt.ShiftModifier)
                    event.accepted = false
                else
                    dialog.finish()
            }
            Keys.onEnterPressed: dialog.finish()
        }

        Label {
            visible: !dialog.asks
            color: "#a0a0a8"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            text: dialog.task.scheduleKind === "once" ? qsTr("This one is done for good.")
                                                      : qsTr("It comes back on its schedule: %1.").arg((dialog.task.scheduleText || "").toLowerCase())
        }

        Label { visible: dialog.asks; text: qsTr("When should it come back?"); color: "#c4c4cc" }
        DateStepper {
            id: next
            visible: dialog.asks
            enabled: !notAgain.checked
            Layout.fillWidth: true
            jumps: [
                { label: qsTr("1 week"), amount: 1, unit: "week" },
                { label: qsTr("2 weeks"), amount: 2, unit: "week" },
                { label: qsTr("1 month"), amount: 1, unit: "month" },
                { label: qsTr("3 months"), amount: 3, unit: "month" },
                { label: qsTr("6 months"), amount: 6, unit: "month" },
                { label: qsTr("1 year"), amount: 1, unit: "year" }
            ]
        }
        CheckBox { id: notAgain; visible: dialog.asks; text: qsTr("Not again") }
    }

    footer: DialogButtonBox {
        Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        Button { text: qsTr("Done"); highlighted: true; onClicked: dialog.finish() }
    }
}
