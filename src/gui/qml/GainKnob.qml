import QtQuick

// Rotary control: vertical drag / wheel / double-click-reset not implemented.
Item {
    id: root
    property real value: 0
    property real from: 0
    property real to: 72
    property real step: 1
    property string unit: ""
    property string minLabel: ""
    property string maxLabel: ""
    property bool showValue: true
    signal moved(real v)
    implicitWidth: 112; implicitHeight: 112
    opacity: enabled ? 1 : 0.5
    readonly property real frac: (Math.max(from, Math.min(to, value)) - from) / (to - from)
    readonly property real a0: 0.75 * Math.PI
    readonly property real sweep: 1.5 * Math.PI

    Canvas {
        id: cv
        anchors.fill: parent
        onPaint: {
            var c = getContext("2d"); c.reset();
            var cx = width / 2, cy = height / 2, R = width / 2 - 6;
            // tick marks
            c.strokeStyle = Theme.textDim; c.lineWidth = 1;
            for (var i = 0; i <= 24; ++i) {
                var a = root.a0 + root.sweep * i / 24;
                c.beginPath();
                c.moveTo(cx + Math.cos(a) * (R + 1), cy + Math.sin(a) * (R + 1));
                c.lineTo(cx + Math.cos(a) * (R + 5), cy + Math.sin(a) * (R + 5));
                c.stroke();
            }
            // knob body
            var g = c.createRadialGradient(cx - 8, cy - 10, 4, cx, cy, R);
            g.addColorStop(0, "#3a3a3a"); g.addColorStop(1, "#141414");
            c.fillStyle = g; c.beginPath(); c.arc(cx, cy, R - 4, 0, 2 * Math.PI); c.fill();
            c.strokeStyle = "#555"; c.lineWidth = 1.5; c.stroke();
            // pointer
            var pa = root.a0 + root.sweep * root.frac;
            c.strokeStyle = "#f0f0f0"; c.lineWidth = 2.5; c.lineCap = "round";
            c.beginPath();
            c.moveTo(cx + Math.cos(pa) * (R - 22), cy + Math.sin(pa) * (R - 22));
            c.lineTo(cx + Math.cos(pa) * (R - 9), cy + Math.sin(pa) * (R - 9));
            c.stroke();
        }
        Connections { target: root; function onFracChanged() { cv.requestPaint(); } }
    }
    Text { visible: root.showValue; anchors.centerIn: parent; color: Theme.text; font.pixelSize: 18
           text: Math.round(root.value) + root.unit }
    Text { x: 6; y: parent.height - 10; color: Theme.textDim; font.pixelSize: 8; text: root.minLabel }
    Text { anchors.right: parent.right; anchors.rightMargin: 6; y: parent.height - 10
           color: Theme.textDim; font.pixelSize: 8; text: root.maxLabel }
    MouseArea {
        anchors.fill: parent
        property real startY; property real startV
        onPressed: (m) => { startY = m.y; startV = root.value; }
        onPositionChanged: (m) => {
            if (!pressed) return;
            var v = startV + (startY - m.y) * (root.to - root.from) / 140;
            v = Math.max(root.from, Math.min(root.to, v));
            root.moved(Math.round(v / root.step) * root.step);
        }
        onWheel: (w) => root.moved(Math.max(root.from, Math.min(root.to,
                              root.value + (w.angleDelta.y > 0 ? root.step : -root.step))))
    }
}
