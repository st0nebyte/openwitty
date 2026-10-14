import QtQuick

Row {
    id: root
    property string state: "Disconnected"
    property string detail: ""
    spacing: 8
    Rectangle {
        width: 12; height: 12; radius: 6; anchors.verticalCenter: parent.verticalCenter
        color: root.state === "Connected" ? Theme.ok
             : root.state === "Connecting" ? Theme.warn
             : root.state === "Error" ? Theme.bad : Theme.textDim
    }
    Text {
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.text; font.pixelSize: 13
        text: root.state + (root.detail.length ? "  ·  " + root.detail : "")
    }
}
