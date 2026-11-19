import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Hides a task until a day when it's worth seeing again.
Dialog {
    id: dialog
    anchors.centerIn: parent
    modal: true
    width: Math.min(parent.width - 48, 420)
    title: qsTr("Snooze: %1").arg(task.title || "")
    Material.roundedScale: theme.radius

    property var task: ({})
    readonly property var choices: [
        { label: qsTr("Tomorrow"), days: 1 },
        { label: qsTr("3 days"), days: 3 },
        { label: qsTr("1 week"), days: 7 },
        { label: qsTr("2 weeks"), days: 14 },
        { label: qsTr("1 month"), days: 30 },
        { label: qsTr("3 months"), days: 91 }
    ]

    function openFor(t) {
        task = t
        open()
        grid.forceActiveFocus()
    }
    function pick(days) {
        backend.snooze(task.id, days)
        close()
    }

    GridLayout {
        id: grid
        width: parent.width
        columns: 2
        columnSpacing: 8
        rowSpacing: 4
        focus: true
        Keys.onPressed: event => {
            const n = event.key - Qt.Key_1
            if (n >= 0 && n < dialog.choices.length) {
                dialog.pick(dialog.choices[n].days)
                event.accepted = true
            }
        }
        Repeater {
            model: dialog.choices
            Button {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                flat: true
                text: (index + 1) + "   " + modelData.label
                onClicked: dialog.pick(modelData.days)
            }
        }
    }

    footer: DialogButtonBox {
        Button {
            visible: dialog.task.state === "snoozed"
            text: qsTr("Wake it now")
            flat: true
            onClicked: { backend.unsnooze(dialog.task.id); dialog.close() }
        }
        Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
    }
}
