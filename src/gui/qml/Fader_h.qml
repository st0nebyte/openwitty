import QtQuick

// Horizontal 0..100 slider for settings pages.
Item {
    id: root
    property real value: 0
    signal moved(int v)
    implicitHeight: 20
    opacity: enabled ? 1 : 0.5
    Rectangle { y: 8; width: parent.width; height: 4; radius: 2; color: Theme.well; border.color: Theme.line }
    Rectangle { y: 8; width: parent.width * root.value / 100; height: 4; radius: 2; color: Theme.accentDim }
    Rectangle { x: (parent.width - 12) * root.value / 100; y: 3; width: 12; height: 14; radius: 2; color: "#bdbdbd" }
    MouseArea { anchors.fill: parent
        onPressed: (m) => root.moved(Math.round(Math.max(0, Math.min(1, m.x / width)) * 100))
        onPositionChanged: (m) => { if (pressed) root.moved(Math.round(Math.max(0, Math.min(1, m.x / width)) * 100)); } }
}
