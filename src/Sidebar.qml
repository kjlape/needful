import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The lists, then the contexts that filter them.
Rectangle {
    id: sidebar
    color: "#121215"
    Material.roundedScale: theme.radius

    property var views: []
    signal addContext(int parent)
    signal renameContext(var context)
    signal removeContext(var context)

    component SideItem: ItemDelegate {
        id: item
        property string label
        property int count: 0
        property bool active: false
        property int depth: 0
        Layout.fillWidth: true
        implicitHeight: 34
        leftPadding: 16 + depth * 14
        rightPadding: 12
        background: Rectangle {
            radius: Math.min(4, theme.radius)
            color: item.active ? Qt.rgba(1, 1, 1, 0.08) : item.hovered ? Qt.rgba(1, 1, 1, 0.04) : "transparent"
            Rectangle {
                width: 3
                height: parent.height - 12
                anchors.verticalCenter: parent.verticalCenter
                color: theme.accent
                visible: item.active
            }
        }
        contentItem: RowLayout {
            spacing: 8
            Label {
                text: item.label
                elide: Text.ElideRight
                Layout.fillWidth: true
                color: item.active ? "#ffffff" : "#c4c4cc"
                font.bold: item.active
            }
            Label {
                text: item.count > 0 ? item.count.toLocaleString(Qt.locale(), "f", 0) : ""
                color: item.active ? theme.accent : "#77777f"
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        anchors.topMargin: 18
        spacing: 1

        Label {
            text: "Needful"
            font.pixelSize: 18
            font.bold: true
            color: theme.accent
            Layout.leftMargin: 16
            Layout.bottomMargin: 14
        }

        Repeater {
            model: sidebar.views
            SideItem {
                required property var modelData
                required property int index
                label: modelData.label
                count: modelData.key === "history" ? 0 : (backend.counts[modelData.key] || 0)
                active: backend.view === modelData.key
                onClicked: backend.view = modelData.key
                ToolTip.visible: hovered
                ToolTip.delay: 800
                ToolTip.text: (index + 1).toString()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 22
            Layout.leftMargin: 16
            Label {
                text: qsTr("Contexts")
                color: "#77777f"
                font.bold: true
                font.capitalization: Font.AllUppercase
                font.pixelSize: 11
                Layout.fillWidth: true
            }
            ToolButton {
                text: "+"
                font.pixelSize: 18
                implicitHeight: 30
                implicitWidth: 30
                onClicked: sidebar.addContext(backend.contextFilter)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("New context (C)")
            }
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: contextColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            ColumnLayout {
                id: contextColumn
                width: parent.width
                spacing: 1

                SideItem {
                    label: qsTr("Everything")
                    active: backend.contextFilter === 0
                    onClicked: backend.contextFilter = 0
                }

                Repeater {
                    model: backend.contexts
                    SideItem {
                        id: contextItem
                        required property var modelData
                        label: modelData.name
                        depth: modelData.depth
                        count: modelData.due
                        active: backend.contextFilter === modelData.id
                        onClicked: backend.contextFilter = modelData.id
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: menu.popup()
                        }
                        Menu {
                            id: menu
                            Material.roundedScale: theme.radius
                            MenuItem { text: qsTr("New context inside"); onTriggered: sidebar.addContext(contextItem.modelData.id) }
                            MenuItem { text: qsTr("Rename"); onTriggered: sidebar.renameContext(contextItem.modelData) }
                            MenuItem { text: qsTr("Delete"); onTriggered: sidebar.removeContext(contextItem.modelData) }
                        }
                    }
                }

                Label {
                    visible: backend.contexts.length === 0
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 12
                    Layout.topMargin: 8
                    wrapMode: Text.WordWrap
                    color: "#6e6e78"
                    text: qsTr("Group tasks by where you do them: Home, a car, a client. Contexts can sit inside each other.")
                }
            }
        }

        // Quiet while every machine is heard from; plain words when one isn't.
        Label {
            visible: backend.syncText !== ""
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 12
            Layout.bottomMargin: 2
            wrapMode: Text.WordWrap
            text: backend.syncText
            color: backend.syncLate ? theme.accent : "#5a5a63"
            font.pixelSize: 12
        }

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.bottomMargin: 8
            text: qsTr("? for shortcuts")
            color: "#5a5a63"
            font.pixelSize: 12
        }
    }
}
