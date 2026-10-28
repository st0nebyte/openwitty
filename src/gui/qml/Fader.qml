import QtQuick

// Slim vertical fader: dark slot, light-grey rail, flat rounded cap with centre line.
//   commitOnRelease false (simulated device): moved(v) on press and on every pointer movement.
//   commitOnRelease true (real device, Phase 11A): dragging only previews; committed(v) once on release, and only if the
//   value differs from the one at the press. Wheel notches preview too and commit once after the wheel rests.
//   Nothing is emitted when `value` changes from outside.
Item {
    id: root
    property real value: 0
    property real from: -60
    property real to: 0
    property bool commitOnRelease: false
    readonly property bool previewing: ma.pressed || wheelTimer.running
    property real previewValue: 0
    property real pressValue: 0
    // After a commit the new value stays shown until the device reports it (or 1.5 s pass without it, e.g. a refused
    // write); without this the old value flashed up between the end of the preview and the confirmed read.
    property bool awaiting: false
    readonly property real shownValue: (previewing || awaiting) && commitOnRelease ? previewValue : value
    onValueChanged: awaiting = false
    Timer { id: awaitTimer; interval: 1500; onTriggered: root.awaiting = false }
    function commit(v) { awaiting = true; awaitTimer.restart(); committed(v); }
    signal moved(real v)
    signal committed(real v)
    implicitWidth: 20; implicitHeight: 187
    opacity: enabled ? 1 : 0.5
    readonly property real frac: (Math.max(from, Math.min(to, shownValue)) - from) / (to - from)
    function clampStep(v) { return Math.round(Math.max(from, Math.min(to, v)) * 2) / 2; }
    function emitValue(v) { previewValue = v; if (!commitOnRelease) moved(v); }
    Timer { id: wheelTimer; interval: 400
            onTriggered: if (root.commitOnRelease && root.previewValue !== root.pressValue) root.commit(root.previewValue) }
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
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        onEntered: Hint.show(root.tip)
        onExited: Hint.clear()
        function setFrom(y) {
            var f = 1 - Math.max(0, Math.min(1, (y - handle.height / 2) / (root.height - handle.height)));
            root.emitValue(root.clampStep(root.from + f * (root.to - root.from)));
        }
        onPressed: (m) => { if (!wheelTimer.running) root.pressValue = root.value; setFrom(m.y); }
        onPositionChanged: (m) => { if (pressed) setFrom(m.y); }
        onReleased: (m) => {
            setFrom(m.y);
            if (root.commitOnRelease && root.previewValue !== root.pressValue) { wheelTimer.stop(); root.commit(root.previewValue); }
        }
        onWheel: (w) => {
            if (!wheelTimer.running) { root.pressValue = root.value; root.previewValue = root.value; }
            root.emitValue(root.clampStep((root.commitOnRelease ? root.previewValue : root.value) + (w.angleDelta.y > 0 ? 0.5 : -0.5)));
            if (root.commitOnRelease) wheelTimer.restart();
        }
    }
}
