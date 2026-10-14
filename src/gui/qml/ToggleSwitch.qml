import QtQuick

Item {
    id: root
    property bool checked: false
    property string label: ""
    property bool unverified: true
    signal toggled(bool value)
    implicitWidth: 150; implicitHeight: 30
    opacity: enabled ? 1 : 0.45
    Row {
        spacing: 10; anchors.verticalCenter: parent.verticalCenter
        Rectangle {
            width: 44; height: 24; radius: 12
            color: root.checked ? Theme.accent : Theme.panelHi
            border.color: Theme.line
            Rectangle {
                width: 18; height: 18; radius: 9; y: 3
                x: root.checked ? 23 : 3
                color: "#fff"
                Behavior on x { NumberAnimation { duration: 90 } }
            }
        }
        Text { text: root.label; color: Theme.text; font.pixelSize: 13
               anchors.verticalCenter: parent.verticalCenter }
    }
    MouseArea { anchors.fill: parent; onClicked: root.toggled(!root.checked) }
}
