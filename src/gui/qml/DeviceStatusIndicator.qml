import QtQuick

Row {
    id: root
    property string state: "Disconnected"
    property string detail: ""
    spacing: 8
    Rectangle {
        width: 10; height: 10; radius: 5; anchors.verticalCenter: parent.verticalCenter
        color: root.state === "Connected" ? Theme.ok
             : root.state === "Connecting" ? Theme.warn
             : root.state === "Error" ? Theme.bad : Theme.textDim
    }
    Text {
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.textDim; font.pixelSize: 11
        text: root.state + (root.detail.length ? "  ·  " + root.detail : "")
    }
}
