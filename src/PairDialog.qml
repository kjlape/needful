import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Pairing your own computers, by a code one shows and the other types. Both
// show the same fingerprint, so you can check you paired with your own.
Dialog {
    id: dialog
    anchors.centerIn: parent
    modal: true
    width: Math.min(parent.width - 48, 480)
    title: mode === "pair" ? qsTr("Pair a computer") : qsTr("Join a computer")
    Material.roundedScale: theme.radius

    property string mode: "pair"
    readonly property string state: peers.state
    readonly property bool asking: mode === "join" && (state === "" || state === "failed")

    function openPair() {
        mode = "pair"
        open()
        peers.pair()
    }

    function openJoin() {
        mode = "join"
        peers.cancel()
        codeField.text = ""
        atField.text = ""
        open()
        codeField.forceActiveFocus()
    }

    function join() {
        if (codeField.text.trim() !== "")
            peers.join(codeField.text.trim(), atField.text.trim())
    }

    onClosed: {
        if (state !== "done")
            peers.cancel()
    }

    ColumnLayout {
        width: parent.width
        spacing: 12

        // Showing a code.
        Label {
            visible: dialog.mode === "pair" && dialog.state === "pairing"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: "#a0a0a8"
            text: qsTr("On the other computer, choose Join a computer and type this code:")
        }
        TextEdit {
            visible: dialog.mode === "pair" && dialog.state === "pairing"
            readOnly: true
            selectByMouse: true
            text: peers.code
            color: theme.accent
            font.pixelSize: 24
            font.bold: true
        }
        Label {
            visible: dialog.mode === "pair" && dialog.state === "pairing"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("This computer is “%1”  %2. The code works once, for a few minutes.")
                  .arg(peers.thisName).arg(peers.thisFingerprint)
        }

        // Typing one.
        TextField {
            id: codeField
            visible: dialog.asking
            Layout.fillWidth: true
            placeholderText: qsTr("Code, like 7-violet-crumble-tango")
            onAccepted: dialog.join()
        }
        TextField {
            id: atField
            visible: dialog.asking
            Layout.fillWidth: true
            placeholderText: qsTr("Its address, if it's on another network (optional)")
            onAccepted: dialog.join()
        }

        // Checking it's yours.
        Label {
            visible: dialog.state === "confirm"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Pair with “%1”  %2? Check that the other computer shows the same.")
                  .arg(peers.otherName).arg(peers.otherFingerprint)
        }

        RowLayout {
            visible: dialog.state === "waiting" || dialog.state === "joining"
            spacing: 12
            BusyIndicator { running: parent.visible; implicitWidth: 32; implicitHeight: 32 }
            Label {
                text: dialog.state === "waiting" ? qsTr("Getting a code…") : qsTr("Joining…")
                color: "#a0a0a8"
            }
        }

        Label {
            visible: dialog.state === "done" || dialog.state === "failed"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: peers.message
            color: dialog.state === "failed" ? theme.accent : "#e4e4ea"
        }
    }

    footer: DialogButtonBox {
        Button {
            text: dialog.state === "done" ? qsTr("Done") : qsTr("Cancel")
            flat: true
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        Button {
            visible: dialog.asking
            text: qsTr("Join")
            highlighted: true
            enabled: codeField.text.trim() !== ""
            onClicked: dialog.join()
        }
        Button {
            visible: dialog.state === "confirm"
            text: qsTr("Pair")
            highlighted: true
            onClicked: peers.answer(true)
        }
        Button {
            visible: dialog.mode === "pair" && dialog.state === "failed"
            text: qsTr("New code")
            highlighted: true
            onClicked: peers.pair()
        }
    }
}
