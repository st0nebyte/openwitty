import QtQuick

// Slim vertical fader: dark slot, light-grey rail, flat rounded cap with centre line.
Item {
    id: root
    property real value: 0
    property real from: -60
    property real to: 0
    signal moved(real v)
    implicitWidth: 20; implicitHeight: 187
    opacity: enabled ? 1 : 0.5
    readonly property real frac: (Math.max(from, Math.min(to, value)) - from) / (to - from)
    Rectangle { x: parent.width / 2 - 2; width: 4; height: parent.height; color: "#2a2a2a" }
    Rectangle { x: parent.width / 2 - 1; width: 2; y: handle.y + handle.height / 2
                height: parent.height - y; color: "#8f8d8d" }
    Rectangle {
        id: handle
        width: 19; height: 7; radius: 2; color: "#ffffff"; border.color: "#bdbdbd"
        x: parent.width / 2 - width / 2
        y: (1 - root.frac) * (root.height - height)
        Rectangle { width: parent.width - 6; height: 1; x: 3; anchors.verticalCenter: parent.verticalCenter; color: "#555" }
    }
    property string tip: "Output volume"
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onEntered: Hint.show(root.tip)
        onExited: Hint.clear()
        function setFrom(y) {
            var f = 1 - Math.max(0, Math.min(1, (y - handle.height / 2) / (root.height - handle.height)));
            root.moved(Math.round((root.from + f * (root.to - root.from)) * 2) / 2);
        }
        onPressed: (m) => setFrom(m.y)
        onPositionChanged: (m) => { if (pressed) setFrom(m.y); }
    }
}
