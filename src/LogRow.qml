import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A note in the history: what was noticed, where, and whether it's been
// followed up. A note about a time of a task says which task.
Rectangle {
    id: row
    property var entry: ({})
    property bool current: false
    signal picked()

    Material.roundedScale: theme.radius
    implicitHeight: content.implicitHeight + 22
    radius: Math.min(6, theme.radius)
    color: current ? "#1d1d22" : hover.hovered ? "#16161a" : "transparent"
    border.color: current ? Qt.rgba(1, 1, 1, 0.08) : "transparent"

    readonly property bool hasFollowUp: (entry.task || 0) > 0

    HoverHandler { id: hover }
    TapHandler { onTapped: row.picked() }

    RowLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 12
        anchors.rightMargin: 16
        spacing: 12

        // A note mark rather than a check: this was noticed, not done.
        Rectangle {
            width: 22
            height: 22
            radius: Math.min(4, theme.radius)
            color: "transparent"
            border.width: 1.5
            border.color: "#85858e"
            Layout.alignment: Qt.AlignTop
            Column {
                anchors.centerIn: parent
                spacing: 3
                Repeater {
                    model: 3
                    Rectangle { width: index === 2 ? 6 : 10; height: 1.5; color: "#85858e" }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3
            Label {
                textFormat: Text.PlainText
                text: (row.entry.about ? (row.entry.aboutTitle || "") + " · " : "") + (row.entry.text || "")
                color: "#ececf0"
                font.pixelSize: 15
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Label {
                visible: text !== ""
                text: row.entry.contextPath || ""
                font.pixelSize: 12
                color: "#85858e"
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Label {
                visible: row.hasFollowUp
                font.pixelSize: 12
                color: row.entry.followUpDone ? "#85858e" : theme.accent
                text: row.entry.followUpDone ? qsTr("Followed up ✓")
                                             : qsTr("Follow-up: %1").arg(row.entry.followUpDueText || "")
                Layout.topMargin: 2
            }
        }

        Label {
            Layout.alignment: Qt.AlignTop
            text: row.entry.date || ""
            font.pixelSize: 12
            color: "#85858e"
        }
    }
}
