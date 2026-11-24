import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Who a universe is shared with: your computers, then everyone else in it,
// each with Remove. Removing someone stops what comes after; what they have
// stays theirs.
Dialog {
    id: dialog
    anchors.centerIn: parent
    modal: true
    width: Math.min(parent.width - 48, 480)
    title: qsTr("People in “%1”").arg(label)
    Material.roundedScale: theme.radius

    property string universe: ""
    property string label: ""
    property var people: ({ yours: "", others: [] })
    property string problem: ""
    signal removeAsked(var member)
    signal shareAsked()

    function openFor(u) {
        universe = u.name
        label = u.label
        problem = ""
        reload()
        open()
    }

    function reload() { people = peers.members(universe) }

    function remove(member) {
        problem = peers.unshare(universe, member.id)
        reload()
    }

    // Who's connected changes while it's open.
    Timer { interval: 3000; repeat: true; running: dialog.visible; onTriggered: dialog.reload() }

    ColumnLayout {
        width: parent.width
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Label { text: qsTr("Your computers"); color: "#a0a0a8" }
            Label { text: dialog.people.yours; Layout.fillWidth: true; elide: Text.ElideRight }
        }

        Repeater {
            model: dialog.people.others
            RowLayout {
                id: row
                required property var modelData
                Layout.fillWidth: true
                spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Label { text: row.modelData.name; elide: Text.ElideRight; Layout.fillWidth: true }
                    Label {
                        text: row.modelData.fingerprint + "  ·  " + row.modelData.status
                        color: "#77777f"
                        font.pixelSize: 12
                    }
                }
                Button {
                    visible: row.modelData.removable
                    text: qsTr("Remove")
                    flat: true
                    onClicked: dialog.removeAsked(row.modelData)
                }
            }
        }

        Label {
            visible: dialog.people.others.length === 0
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: "#a0a0a8"
            text: qsTr("“%1” is only on your computers so far.").arg(dialog.label)
        }

        Label {
            visible: dialog.problem !== ""
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: dialog.problem
            color: theme.accent
        }
    }

    footer: DialogButtonBox {
        Button {
            text: qsTr("Done")
            flat: true
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        Button {
            text: qsTr("Share…")
            highlighted: true
            onClicked: {
                dialog.close()
                dialog.shareAsked()
            }
        }
    }
}
