import QtQuick

Rectangle {
    id: root
    property alias text: label.text
    property bool active: false
    signal clicked()
    implicitWidth: 78; implicitHeight: 30
    radius: 6
    color: active ? Theme.accent : (mouse.containsMouse ? Theme.panelHi : Theme.panel)
    border.color: active ? Theme.accent : Theme.line
    opacity: enabled ? 1 : 0.45
    Text { id: label; anchors.centerIn: parent; font.pixelSize: 12
           color: root.active ? "#111" : Theme.text }
    MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true; onClicked: root.clicked() }
}
