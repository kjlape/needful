import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One entry in the history: what was done, or skipped, when, and the note
// left for next time, or why it was skipped.
Rectangle {
    id: row
    property var entry: ({})
    property bool current: false
    property bool compact: false
    signal picked()

    Material.roundedScale: theme.radius
    implicitHeight: content.implicitHeight + (compact ? 14 : 22)
    radius: Math.min(6, theme.radius)
    color: current ? "#1d1d22" : hover.hovered && !compact ? "#16161a" : "transparent"
    border.color: current ? Qt.rgba(1, 1, 1, 0.08) : "transparent"

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

        Rectangle {
            width: 22
            height: 22
            radius: 11
            color: Qt.rgba(1, 1, 1, 0.06)
            Layout.alignment: Qt.AlignTop
            Label {
                anchors.centerIn: parent
                text: row.entry.skipped ? "↷" : "✓"
                color: row.entry.skipped ? "#85858e" : theme.accent
                font.pixelSize: 12
                font.bold: true
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3
            Label {
                text: row.entry.skipped ? qsTr("Skipped: %1").arg(row.entry.title || "") : row.entry.title || ""
                color: row.compact ? "#9a9aa3" : "#ececf0"
                font.pixelSize: row.compact ? 14 : 15
                font.strikeout: row.compact
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Label {
                visible: !row.compact && text !== ""
                text: row.entry.contextPath || ""
                font.pixelSize: 12
                color: "#85858e"
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Label {
                visible: (row.entry.note || "") !== ""
                text: row.entry.note || ""
                wrapMode: Text.Wrap
                color: "#c4c4cc"
                font.pixelSize: 13
                Layout.fillWidth: true
                Layout.topMargin: 2
            }
        }

        Label {
            Layout.alignment: Qt.AlignTop
            text: row.compact ? row.entry.time : row.entry.date
            font.pixelSize: 12
            color: "#85858e"
        }
    }
}
