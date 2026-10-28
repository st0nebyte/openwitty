import QtQuick

// Rotary control: glossy dark sphere, tick ring, short white pointer, small grey
// value, min/max captions. Vertical drag / wheel; double-click resets to `reset`.
Item {
    id: root
    property real value: 0
    property real from: 0
    property real to: 72
    property real step: 1
    property real bodyRadius: 33
    property string unit: ""
    property string minLabel: ""
    property string maxLabel: ""
    property bool showValue: true
    property string tip: ""
    property real warnFrom: 0.9     // fraction of range where the orange arc starts (approximation)
    property real sweepDeg: 270
    property int labelInset: 13
    // Phase 11A, real device: drag/wheel preview only; one committed(v) on release (or after the wheel rests),
    // only if the value changed. Simulated device: moved(v) continuously as before.
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
    implicitWidth: 2 * (bodyRadius + 12); implicitHeight: implicitWidth
    opacity: enabled ? 1 : 0.5
    readonly property real frac: (Math.max(from, Math.min(to, shownValue)) - from) / (to - from)
    function emitValue(v) { previewValue = v; if (!commitOnRelease) moved(v); }
    Timer { id: wheelTimer; interval: 400
            onTriggered: if (root.commitOnRelease && root.previewValue !== root.pressValue) root.commit(root.previewValue) }
    readonly property real a0: (90 + (360 - sweepDeg) / 2) * Math.PI / 180
    readonly property real sweep: sweepDeg * Math.PI / 180

    Canvas {
        id: cv
        anchors.fill: parent
        onPaint: {
            var c = getContext("2d"); c.reset();
            var cx = width / 2, cy = height / 2, R = root.bodyRadius;
            c.strokeStyle = "#a4a4a4"; c.lineWidth = 1.6;
            for (var i = 0; i <= 28; ++i) {
                var a = root.a0 + root.sweep * i / 28, r0 = R + 6, r1 = R + (i % 4 === 0 ? 14 : 12);
                c.beginPath(); c.moveTo(cx + Math.cos(a) * r0, cy + Math.sin(a) * r0);
                c.lineTo(cx + Math.cos(a) * r1, cy + Math.sin(a) * r1); c.stroke();
            }
            var g = c.createRadialGradient(cx - R * 0.25, cy - R * 0.35, R * 0.1, cx, cy, R);
            g.addColorStop(0, "#3d3a38"); g.addColorStop(0.65, "#222120"); g.addColorStop(1, "#141312");
            c.fillStyle = g; c.beginPath(); c.arc(cx, cy, R, 0, 2 * Math.PI); c.fill();
            c.strokeStyle = "#000000"; c.lineWidth = 3; c.stroke();
            if (root.warnFrom < 1 && root.frac > root.warnFrom) {   // orange arc near maximum gain
                c.strokeStyle = "#f8aa37"; c.lineWidth = 2.5; c.lineCap = "butt";
                c.beginPath(); c.arc(cx, cy, R - 1.5, root.a0 + root.sweep * root.warnFrom, root.a0 + root.sweep * root.frac);
                c.stroke();
            }
            var pa = root.a0 + root.sweep * root.frac;
            c.strokeStyle = "#f2f2f2"; c.lineWidth = 2.8; c.lineCap = "round";
            c.beginPath();
            c.moveTo(cx + Math.cos(pa) * (R - 14), cy + Math.sin(pa) * (R - 14));
            c.lineTo(cx + Math.cos(pa) * (R - 6), cy + Math.sin(pa) * (R - 6));
            c.stroke();
        }
        Connections { target: root; function onFracChanged() { cv.requestPaint(); } }
    }
    Text { visible: root.showValue; anchors.centerIn: parent; color: "#ffffff"; font.pixelSize: 13; font.bold: true
           text: Math.round(root.shownValue) + root.unit }
    Text { x: root.labelInset; y: parent.height - 14; color: Theme.textDim; font.pixelSize: 7; text: root.minLabel }
    Text { anchors.right: parent.right; anchors.rightMargin: root.labelInset; y: parent.height - 14
           color: Theme.textDim; font.pixelSize: 7; text: root.maxLabel }
    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        onEntered: if (root.tip.length) Hint.show(root.tip)
        onExited: Hint.clear()
        property real startY; property real startV
        onPressed: (m) => {
            startY = m.y;
            if (!wheelTimer.running) root.pressValue = root.value;
            startV = wheelTimer.running ? root.previewValue : root.value;
            root.previewValue = startV; wheelTimer.stop();
        }
        onPositionChanged: (m) => {
            if (!pressed) return;
            var v = startV + (startY - m.y) * (root.to - root.from) / 140;
            v = Math.max(root.from, Math.min(root.to, v));
            root.emitValue(Math.round(v / root.step) * root.step);
        }
        onReleased: if (root.commitOnRelease && root.previewValue !== root.pressValue) root.commit(root.previewValue)
        onWheel: (w) => {
            if (!wheelTimer.running) { root.pressValue = root.value; root.previewValue = root.value; }
            var base = root.commitOnRelease ? root.previewValue : root.value;
            root.emitValue(Math.max(root.from, Math.min(root.to, base + (w.angleDelta.y > 0 ? root.step : -root.step))));
            if (root.commitOnRelease) wheelTimer.restart();
        }
    }
}
