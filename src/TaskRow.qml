import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One task in a list: what it is, where, and how needful.
Rectangle {
    id: row
    property var task: ({})
    property bool current: false
    signal picked()
    signal completeRequested()
    signal todayRequested()
    signal snoozeRequested()
    signal editRequested()

    Material.roundedScale: theme.radius
    implicitHeight: 58
    radius: Math.min(6, theme.radius)
    color: current ? "#1d1d22" : hover.hovered ? "#16161a" : "transparent"
    border.color: current ? Qt.rgba(1, 1, 1, 0.08) : "transparent"

    readonly property color priorityColor: ["#3a3a42", "#3a3a42", "#6c6c76", theme.accent, "#ff5c5c"][task.priority || 2]
    readonly property bool late: (task.overdue || 0) > 0

    HoverHandler { id: hover }
    TapHandler { onTapped: row.picked(); onDoubleTapped: row.editRequested() }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 8
        spacing: 12

        // Round check: click to mark done.
        Rectangle {
            width: 22
            height: 22
            radius: 11
            color: check.hovered ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
            border.width: 2
            border.color: check.hovered ? theme.accent : row.priorityColor
            HoverHandler { id: check; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: row.completeRequested() }
            ToolTip.visible: check.hovered
            ToolTip.delay: 600
            ToolTip.text: qsTr("Done (Space)")
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3
            RowLayout {
                spacing: 8
                Layout.fillWidth: true
                Label {
                    text: row.task.title || ""
                    color: "#ececf0"
                    font.pixelSize: 15
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                font.pixelSize: 12
                color: "#85858e"
                text: [row.task.contextPath, row.task.lastDone].filter(s => s).join("  ·  ")
            }
        }

        Rectangle {
            visible: row.task.today === true && backend.view !== "today"
            radius: Math.min(4, theme.radius)
            color: Qt.rgba(1, 1, 1, 0.06)
            implicitWidth: todayLabel.implicitWidth + 14
            implicitHeight: 22
            Label { id: todayLabel; anchors.centerIn: parent; text: qsTr("Today"); font.pixelSize: 11; color: theme.accent }
        }

        Label {
            text: row.task.dueText || ""
            font.pixelSize: 12
            color: row.late ? (row.task.priority >= 4 ? "#ff7a7a" : theme.accent) : "#85858e"
            visible: !actions.visible
        }

        // Quick actions on the row under the pointer.
        RowLayout {
            id: actions
            spacing: 0
            visible: hover.hovered
            ToolButton {
                text: row.task.today ? qsTr("Later") : qsTr("Today")
                font.pixelSize: 12
                onClicked: row.todayRequested()
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: row.task.today ? qsTr("Take off today's list (T)") : qsTr("Pull into today (T)")
            }
            ToolButton {
                text: qsTr("Snooze")
                font.pixelSize: 12
                onClicked: row.snoozeRequested()
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Hide it for a while (S)")
            }
        }
    }
}
