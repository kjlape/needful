import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Marks a task done, with a note for next time, or skips this time of it,
// with why; and for tasks that ask, when it should come back.
Dialog {
    id: dialog
    anchors.centerIn: parent
    modal: true
    width: Math.min(parent.width - 48, 520)
    title: (skipping ? qsTr("Skip: %1") : qsTr("Done: %1")).arg(task.title || "")
    Material.roundedScale: theme.radius

    property var task: ({})
    property bool skipping: false
    readonly property bool asks: task.scheduleKind === "ask"
    // Done with boxes left: say which, and ask to confirm.
    readonly property var unchecked: skipping ? [] : (task.steps || []).filter(s => !s.checked)

    function openFor(t, skip) {
        task = t
        skipping = skip === true
        note.text = ""
        notAgain.checked = false
        next.date = backend.suggestedNext(t.id)
        open()
        note.forceActiveFocus()
    }

    function finish() {
        const nextDue = asks && !notAgain.checked ? next.date : ""
        if (skipping)
            backend.skip(task.id, note.text, nextDue)
        else
            backend.complete(task.id, note.text, nextDue)
        close()
    }

    ColumnLayout {
        width: parent.width
        spacing: 12

        ColumnLayout {
            visible: dialog.unchecked.length > 0
            Layout.fillWidth: true
            spacing: 2
            Label {
                text: qsTr("%n steps aren't checked:", "", dialog.unchecked.length)
                color: theme.accent
                Layout.fillWidth: true
            }
            Repeater {
                model: dialog.unchecked
                Label {
                    required property var modelData
                    text: "– " + modelData.text
                    color: "#c4c4cc"
                    wrapMode: Text.Wrap
                    leftPadding: 12
                    Layout.fillWidth: true
                }
            }
        }

        TextArea {
            id: note
            Layout.fillWidth: true
            Layout.minimumHeight: 90
            wrapMode: TextEdit.Wrap
            placeholderText: dialog.skipping ? qsTr("Why skip it this time? (optional)")
                           : dialog.unchecked.length > 0 ? qsTr("Why they weren't done (optional)")
                           : qsTr("Note for next time (optional)")
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
            text: dialog.skipping ? dialog.task.skipText || ""
                : dialog.task.scheduleKind === "once" ? qsTr("This one is done for good.")
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
        Button {
            text: dialog.skipping ? qsTr("Skip") : dialog.unchecked.length > 0 ? qsTr("Done anyway") : qsTr("Done")
            highlighted: true
            onClicked: dialog.finish()
        }
    }
}
