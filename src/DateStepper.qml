import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Picks a day by nudging it or jumping ahead, which is how due dates get
// chosen in practice ("a month from now"), with no calendar grid to aim at.
ColumnLayout {
    id: stepper
    property string date: backend.dateAfter(0, "day") // ISO, YYYY-MM-DD
    property var jumps: [
        { label: qsTr("Today"), amount: 0, unit: "day" },
        { label: qsTr("Tomorrow"), amount: 1, unit: "day" },
        { label: qsTr("1 week"), amount: 1, unit: "week" },
        { label: qsTr("1 month"), amount: 1, unit: "month" }
    ]
    spacing: 6
    Material.roundedScale: theme.radius

    function shifted(days) {
        const parts = date.split("-").map(Number)
        const d = new Date(parts[0], parts[1] - 1, parts[2] + days)
        const pad = n => (n < 10 ? "0" : "") + n
        return d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate())
    }

    RowLayout {
        spacing: 4
        ToolButton { text: "−"; font.pixelSize: 18; onClicked: stepper.date = stepper.shifted(-1); ToolTip.visible: hovered; ToolTip.text: qsTr("A day earlier") }
        Label {
            text: { translator.language; return backend.formatDate(stepper.date) }
            color: "#ececf0"
            font.pixelSize: 15
            horizontalAlignment: Text.AlignHCenter
            Layout.minimumWidth: 220
        }
        ToolButton { text: "+"; font.pixelSize: 18; onClicked: stepper.date = stepper.shifted(1); ToolTip.visible: hovered; ToolTip.text: qsTr("A day later") }
    }

    Flow {
        Layout.fillWidth: true
        spacing: 4
        Repeater {
            model: stepper.jumps
            Button {
                required property var modelData
                readonly property string target: backend.dateAfter(modelData.amount, modelData.unit)
                text: modelData.label
                flat: true
                checkable: true
                checked: stepper.date === target
                font.pixelSize: 12
                onClicked: stepper.date = target
            }
        }
    }
}
