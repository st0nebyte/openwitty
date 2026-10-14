import QtQuick

// Rotary gain control: vertical drag / wheel. Emits moved(value) in dB.
Item {
    id: root
    property real value: 0
    property real from: 0
    property real to: 85
    property string label: "GAIN"
    property real step: 0.5
    signal moved(real v)
    implicitWidth: 110; implicitHeight: 130
    opacity: enabled ? 1 : 0.45

    function clampv(v) { return Math.max(from, Math.min(to, v)); }
    readonly property real frac: (clampv(value) - from) / (to - from)

    Canvas {
        id: cv
        width: 90; height: 90; anchors.horizontalCenter: parent.horizontalCenter
        onPaint: {
            var c = getContext("2d"); c.reset();
            var a0 = 0.75 * Math.PI, a1 = 2.25 * Math.PI;
            c.lineWidth = 7; c.lineCap = "round";
            c.strokeStyle = Theme.panelHi;
            c.beginPath(); c.arc(45, 45, 36, a0, a1); c.stroke();
            c.strokeStyle = Theme.accent;
            c.beginPath(); c.arc(45, 45, 36, a0, a0 + (a1 - a0) * root.frac); c.stroke();
        }
        Connections { target: root; function onFracChanged() { cv.requestPaint(); } }
        Text { anchors.centerIn: parent; color: Theme.text; font.pixelSize: 17; font.bold: true
               text: root.value.toFixed(1) }
    }
    Text { anchors.horizontalCenter: parent.horizontalCenter; y: 96; color: Theme.textDim
           font.pixelSize: 11; text: root.label + " (dB)" }
    MouseArea {
        anchors.fill: parent
        property real startY; property real startV
        onPressed: (m) => { startY = m.y; startV = root.value; }
        onPositionChanged: (m) => {
            if (!pressed) return;
            var v = root.clampv(startV + (startY - m.y) * (root.to - root.from) / 160);
            root.moved(Math.round(v / root.step) * root.step);
        }
        onWheel: (w) => root.moved(root.clampv(root.value + (w.angleDelta.y > 0 ? 1 : -1) * 1.0))
    }
}
