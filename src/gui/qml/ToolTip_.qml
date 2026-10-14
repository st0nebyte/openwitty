import QtQuick

// Minimal tooltip (avoids QtQuick.Controls dependency).
Rectangle {
    id: root
    property string text: ""
    visible: false
    z: 100
    width: tl.implicitWidth + 12; height: tl.implicitHeight + 8
    x: 0; y: -height - 4
    color: "#000000"; border.color: Theme.line; radius: 3
    Text { id: tl; anchors.centerIn: parent; color: Theme.text; font.pixelSize: 10; text: root.text }
}
