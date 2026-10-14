import QtQuick

// Vertical fader, value in dB (-60..0).
Item {
    id: root
    property real value: 0
    property real from: -60
    property real to: 0
    signal moved(real v)
    implicitWidth: 26; implicitHeight: 160
    opacity: enabled ? 1 : 0.5
    readonly property real frac: (Math.max(from, Math.min(to, value)) - from) / (to - from)
    Rectangle { x: parent.width / 2 - 2; width: 4; height: parent.height; color: Theme.well; border.color: Theme.line }
    Rectangle {
        id: handle
        width: 24; height: 12; radius: 2; color: "#bdbdbd"; border.color: "#777"
        x: parent.width / 2 - width / 2
        y: (1 - root.frac) * (root.height - height)
        Rectangle { width: parent.width - 6; height: 1; x: 3; anchors.verticalCenter: parent.verticalCenter; color: "#444" }
    }
    MouseArea {
        anchors.fill: parent
        function setFrom(y) {
            var f = 1 - Math.max(0, Math.min(1, (y - handle.height / 2) / (root.height - handle.height)));
            root.moved(Math.round((root.from + f * (root.to - root.from)) * 2) / 2);
        }
        onPressed: (m) => setFrom(m.y)
        onPositionChanged: (m) => { if (pressed) setFrom(m.y); }
    }
    Text { anchors.horizontalCenter: parent.horizontalCenter; y: -14; color: Theme.textDim; font.pixelSize: 9
           text: Math.round(root.value) + " dB" }
}
