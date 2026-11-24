import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Notes something on a context (a squeak, a drip, an idea) for its history,
// and optionally raises a one-off task to follow it up. A note can instead
// be about one time of a task, coming up or past; then it's in the task's
// context, and the dialog says which time.
Dialog {
    id: dialog
    anchors.centerIn: parent
    modal: true
    width: Math.min(parent.width - 48, 520)
    title: qsTr("Log a note")
    Material.roundedScale: theme.radius

    // What the note is about: a task's coming time (about), a past time
    // (done), or neither, for the context picked here.
    property int about: 0
    property int done: 0
    property string aboutText: ""

    function clearForm() {
        note.text = ""
        followUp.checked = false
        priorityBox.currentIndex = 1
        due.date = backend.dateAfter(0, "day")
    }

    function openFor(contextId) {
        clearForm()
        about = 0
        done = 0
        aboutText = ""
        contextBox.currentIndex = Math.max(0, contextBox.model.findIndex(c => c.id === contextId))
        open()
        note.forceActiveFocus()
    }

    function where(path) { return path ? " · " + path : "" }

    // "About: Oil change · Tacoma · this time"
    function openAbout(task) {
        clearForm()
        about = task.id
        done = 0
        aboutText = task.title + where(task.contextPath) + " · " + qsTr("this time")
        open()
        note.forceActiveFocus()
    }

    // "About: Oil change · Tacoma · Nov 21", for a completion or skip.
    function openOn(entry) {
        clearForm()
        about = 0
        done = entry.id
        aboutText = entry.title + where(entry.contextPath) + " · " + entry.date
        open()
        note.forceActiveFocus()
    }

    function save() {
        if (note.text.trim() === "")
            return
        backend.addLog({
            context: contextBox.model[contextBox.currentIndex].id,
            about: about,
            done: done,
            text: note.text,
            followUp: followUp.checked,
            priority: priorityBox.currentIndex + 1,
            due: due.date
        })
        close()
    }

    ColumnLayout {
        width: parent.width
        spacing: 10

        Label {
            visible: dialog.aboutText !== ""
            text: qsTr("About: %1").arg(dialog.aboutText)
            color: "#a0a0a8"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        ComboBox {
            id: contextBox
            visible: dialog.aboutText === ""
            Layout.fillWidth: true
            textRole: "label"
            model: [{ id: 0, label: qsTr("No context") }].concat(
                backend.contexts.map(c => ({ id: c.id, label: "    ".repeat(c.depth) + c.name })))
        }

        TextArea {
            id: note
            Layout.fillWidth: true
            Layout.minimumHeight: 90
            wrapMode: TextEdit.Wrap
            placeholderText: qsTr("What did you notice?")
            // Enter saves; Shift+Enter starts a new line.
            Keys.onReturnPressed: event => {
                if (event.modifiers & Qt.ShiftModifier)
                    event.accepted = false
                else
                    dialog.save()
            }
            Keys.onEnterPressed: dialog.save()
        }

        CheckBox {
            id: followUp
            text: qsTr("Follow up on this")
        }

        GridLayout {
            visible: followUp.checked
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 16
            Label { text: qsTr("Priority"); color: "#a0a0a8" }
            ComboBox {
                id: priorityBox
                Layout.fillWidth: true
                model: [qsTr("Low"), qsTr("Normal"), qsTr("High"), qsTr("Urgent")]
            }
            Label { text: qsTr("Due"); color: "#a0a0a8"; Layout.alignment: Qt.AlignTop; Layout.topMargin: 10 }
            DateStepper { id: due; Layout.fillWidth: true }
        }
    }

    footer: DialogButtonBox {
        Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        Button { text: qsTr("Log"); highlighted: true; enabled: note.text.trim() !== ""; onClicked: dialog.save() }
    }
}
