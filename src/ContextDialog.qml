import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Adds or renames a context: Home, Automotive, a car inside it, a client.
Dialog {
    id: dialog
    anchors.centerIn: parent
    modal: true
    width: Math.min(parent.width - 48, 420)
    title: contextId ? qsTr("Rename context") : qsTr("New context")
    Material.roundedScale: theme.radius

    property int contextId: 0

    function openForNew(parent) {
        contextId = 0
        name.text = ""
        parentBox.currentIndex = Math.max(0, parentBox.model.findIndex(c => c.id === parent))
        open()
        name.forceActiveFocus()
    }
    function openForRename(c) {
        contextId = c.id
        name.text = c.name
        open()
        name.forceActiveFocus()
        name.selectAll()
    }
    function save() {
        if (name.text.trim() === "")
            return
        if (contextId)
            backend.renameContext(contextId, name.text)
        else
            backend.contextFilter = backend.addContext(name.text, parentBox.model[parentBox.currentIndex].id)
        close()
    }

    ColumnLayout {
        width: parent.width
        spacing: 8
        TextField {
            id: name
            Layout.fillWidth: true
            placeholderText: qsTr("Name, like Home or White Honda CR-V")
            onAccepted: dialog.save()
        }
        Label { visible: !dialog.contextId; text: qsTr("Inside"); color: "#a0a0a8" }
        ComboBox {
            id: parentBox
            visible: !dialog.contextId
            Layout.fillWidth: true
            textRole: "label"
            model: [{ id: 0, label: qsTr("Nothing (top level)") }].concat(
                backend.contexts.map(c => ({ id: c.id, label: "    ".repeat(c.depth) + c.name })))
        }
    }

    footer: DialogButtonBox {
        Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        Button { text: dialog.contextId ? qsTr("Rename") : qsTr("Add"); highlighted: true; enabled: name.text.trim() !== ""; onClicked: dialog.save() }
    }
}
