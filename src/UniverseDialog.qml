import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Adds a universe: another list, like a partner's or the house's, kept on
// this computer alone or syncing through a folder of its own.
Dialog {
    id: dialog
    anchors.centerIn: parent
    modal: true
    width: Math.min(parent.width - 48, 460)
    title: qsTr("New universe")
    Material.roundedScale: theme.radius

    property string problem: ""

    function openForNew() {
        name.text = ""
        folder.text = ""
        machine.text = ""
        problem = ""
        open()
        name.forceActiveFocus()
    }
    function save() {
        if (name.text.trim() === "")
            return
        problem = backend.addUniverse(name.text, folder.text, machine.text)
        if (problem === "")
            close()
    }

    ColumnLayout {
        width: parent.width
        spacing: 8
        TextField {
            id: name
            Layout.fillWidth: true
            placeholderText: qsTr("Name, like hers or house")
            onTextEdited: dialog.problem = ""
            onAccepted: dialog.save()
        }
        Label { text: qsTr("Sync folder (optional)"); color: "#a0a0a8" }
        TextField {
            id: folder
            Layout.fillWidth: true
            placeholderText: qsTr("Like ~/Sync/needful-hers")
            onTextEdited: dialog.problem = ""
            onAccepted: dialog.save()
        }
        TextField {
            id: machine
            visible: folder.text.trim() !== ""
            Layout.fillWidth: true
            placeholderText: qsTr("This computer's name there (optional)")
            onAccepted: dialog.save()
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: "#6e6e78"
            font.pixelSize: 12
            text: folder.text.trim() === ""
                  ? qsTr("Without a folder, the list stays on this computer.")
                  : qsTr("If others already sync through this folder, you'll join their list.")
        }
        Label {
            visible: dialog.problem !== ""
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: theme.accent
            text: dialog.problem
        }
    }

    footer: DialogButtonBox {
        Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        Button { text: qsTr("Add"); highlighted: true; enabled: name.text.trim() !== ""; onClicked: dialog.save() }
    }
}
