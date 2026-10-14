import QtQuick

Item {
    id: root
    property bool checked: false
    property string label: ""
    signal toggled(bool value)
    implicitWidth: 300; implicitHeight: 26
    opacity: enabled ? 1 : 0.45
    Text { text: root.label; color: Theme.text; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
    Rectangle {
        width: 40; height: 18; radius: 9; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
        color: root.checked ? Theme.accentDim : "#444"
        Rectangle { width: 14; height: 14; radius: 7; y: 2; x: root.checked ? 24 : 2; color: "#ddd"
                    Behavior on x { NumberAnimation { duration: 80 } } }
    }
    MouseArea { anchors.fill: parent; onClicked: root.toggled(!root.checked) }
}
