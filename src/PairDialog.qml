import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Pairing your own computers, or sharing one universe with someone else's,
// by a code one shows and the other types. Both show the same fingerprint,
// so you can check who you're pairing or sharing with.
//
// mode is pair or share (showing a code), or join or joinUniverse (typing one).
Dialog {
    id: dialog
    anchors.centerIn: parent
    modal: true
    width: Math.min(parent.width - 48, 480)
    title: mode === "pair" ? qsTr("Pair a computer")
           : mode === "join" ? qsTr("Join a computer")
           : mode === "share" ? qsTr("Share “%1”").arg(label)
           : qsTr("Join a shared universe")
    Material.roundedScale: theme.radius

    property string mode: "pair"
    property string universe: ""
    property string label: ""
    readonly property string state: peers.state
    readonly property bool showing: mode === "pair" || mode === "share"
    readonly property bool asking: !showing && (state === "" || state === "failed")

    function openPair() {
        mode = "pair"
        open()
        peers.pair()
    }

    function openShare(u) {
        mode = "share"
        universe = u.name
        label = u.label
        open()
        peers.share(u.name)
    }

    function openJoin(kind) {
        mode = kind || "join"
        peers.cancel()
        codeField.text = ""
        atField.text = ""
        open()
        codeField.forceActiveFocus()
    }

    function join() {
        if (codeField.text.trim() === "")
            return
        if (mode === "joinUniverse")
            peers.joinUniverse(codeField.text.trim(), atField.text.trim())
        else
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
            visible: dialog.showing && dialog.state === "pairing"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: "#a0a0a8"
            text: dialog.mode === "share"
                  ? qsTr("On their computer, choose Join a shared universe and type this code. They get “%1” and nothing else.").arg(dialog.label)
                  : qsTr("On the other computer, choose Join a computer and type this code:")
        }
        TextEdit {
            visible: dialog.showing && dialog.state === "pairing"
            readOnly: true
            selectByMouse: true
            text: peers.code
            color: theme.accent
            // With a relay the code carries where to find this computer, so
            // it's long and meant to be copied, not read out.
            Layout.fillWidth: true
            wrapMode: TextEdit.WrapAnywhere
            font.pixelSize: peers.code.indexOf(".") >= 0 ? 14 : 24
            font.bold: true
        }
        Label {
            visible: dialog.showing && dialog.state === "pairing"
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
            placeholderText: dialog.mode === "joinUniverse" ? qsTr("Code, like 4-amber-lantern-quiet")
                                                            : qsTr("Code, like 7-violet-crumble-tango")
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
            text: peers.universe !== ""
                  ? qsTr("Join “%1” from “%2”  %3? Check that their computer shows the same. It goes beside your own list, which stays yours.")
                    .arg(peers.universe).arg(peers.otherName).arg(peers.otherFingerprint)
                  : qsTr("Pair with “%1”  %2? Check that the other computer shows the same.")
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
            text: peers.universe !== "" ? qsTr("Join") : qsTr("Pair")
            highlighted: true
            onClicked: peers.answer(true)
        }
        Button {
            visible: dialog.showing && dialog.state === "failed"
            text: qsTr("New code")
            highlighted: true
            onClicked: dialog.mode === "share" ? peers.share(dialog.universe) : peers.pair()
        }
    }
}
