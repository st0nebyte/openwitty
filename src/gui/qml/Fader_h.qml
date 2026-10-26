import QtQuick

// Horizontal 0..100 slider for settings pages.
//   commitOnRelease false (simulated device): moved(v) on press and on every pointer movement, as before.
//   commitOnRelease true (real device): dragging only previews the position; committed(v) is emitted once, on
//   release, and only if the released position differs from the value at the press. Nothing is emitted when
//   `value` changes from outside (bindings, device reads), when the press is cancelled, or while disabled.
Item {
    id: root
    property real value: 0
    property bool commitOnRelease: false
    readonly property bool dragging: ma.pressed
    property int previewValue: 0
    property real pressValue: 0
    readonly property real shownValue: dragging && commitOnRelease ? previewValue : value
    signal moved(int v)
    signal committed(int v)
    implicitHeight: 20
    opacity: enabled ? 1 : 0.5
    function pos(x) { return Math.round(Math.max(0, Math.min(1, x / width)) * 100); }
    Rectangle { y: 8; width: parent.width; height: 4; radius: 2; color: Theme.well; border.color: Theme.line }
    Rectangle { y: 8; width: parent.width * root.shownValue / 100; height: 4; radius: 2
                color: root.dragging && root.commitOnRelease ? Theme.accent : Theme.accentDim }
    Rectangle { x: (parent.width - 12) * root.shownValue / 100; y: 3; width: 12; height: 14; radius: 2; color: "#bdbdbd" }
    MouseArea {
        id: ma
        anchors.fill: parent
        onPressed: (m) => {
            root.pressValue = root.value;
            root.previewValue = root.pos(m.x);
            if (!root.commitOnRelease) root.moved(root.previewValue);
        }
        onPositionChanged: (m) => {
            if (!pressed) return;
            root.previewValue = root.pos(m.x);
            if (!root.commitOnRelease) root.moved(root.previewValue);
        }
        onReleased: (m) => {
            root.previewValue = root.pos(m.x);
            if (root.commitOnRelease && root.previewValue !== Math.round(root.pressValue)) root.committed(root.previewValue);
        }
    }
}
