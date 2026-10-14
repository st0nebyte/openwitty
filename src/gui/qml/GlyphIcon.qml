import QtQuick

// Independently drawn pictograms (mic, guitar, loop, speakers, headphones, shuffle,
// lowcut, clipguard, autogain, laptop). 24x24 design box, scaled to width/height.
Canvas {
    id: root
    property string kind: "mic"
    property color color: "#a8a8a8"
    implicitWidth: 24; implicitHeight: 24
    onKindChanged: requestPaint()
    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onPaint: {
        var c = getContext("2d"); c.reset();
        var s = Math.min(width, height) / 24;
        c.scale(s, s);
        c.strokeStyle = color; c.fillStyle = color; c.lineWidth = 1.6;
        c.lineCap = "round"; c.lineJoin = "round";
        function rr(x, y, w, h, r) {
            c.beginPath(); c.moveTo(x + r, y); c.arcTo(x + w, y, x + w, y + h, r);
            c.arcTo(x + w, y + h, x, y + h, r); c.arcTo(x, y + h, x, y, r); c.arcTo(x, y, x + w, y, r); c.closePath();
        }
        switch (kind) {
        case "mic":   // studio condenser: rectangular head with basket, stem
            rr(7.5, 1, 9, 12.5, 1.6); c.fill();
            c.fillStyle = "#00000055"; c.fillRect(8.8, 8.5, 6.4, 0.9); c.fillStyle = color;
            c.beginPath(); c.moveTo(7.5, 13.5); c.lineTo(16.5, 13.5); c.lineTo(15, 19); c.lineTo(9, 19); c.closePath(); c.fill();
            c.strokeStyle = "#0b0a0a"; c.lineWidth = 1.1;
            c.beginPath(); c.moveTo(9.3, 14.2); c.lineTo(10.2, 17.6); c.moveTo(14.7, 14.2); c.lineTo(13.8, 17.6); c.stroke();
            c.fillStyle = color; rr(10.2, 19, 3.6, 4.5, 0.8); c.fill();
            break;
        case "guitar":   // electric guitar silhouette: body, bridge, neck, headstock
            c.lineWidth = 2.6; c.lineCap = "round";
            c.beginPath(); c.moveTo(11, 13); c.lineTo(20, 4.5); c.stroke();
            c.lineWidth = 3.4; c.beginPath(); c.moveTo(19.5, 3.5); c.lineTo(22, 2); c.stroke();
            c.beginPath(); c.arc(7.2, 15.6, 5.2, 0, 2 * Math.PI); c.fill();
            c.beginPath(); c.arc(11.2, 12.2, 3.4, 0, 2 * Math.PI); c.fill();
            c.beginPath(); c.arc(4.2, 19.6, 3, 0, 2 * Math.PI); c.fill();
            break;
        case "loop":
            rr(2.5, 7, 19, 12, 3); c.stroke();
            c.beginPath(); c.moveTo(14, 3); c.lineTo(9, 7); c.lineTo(14, 11); c.closePath(); c.fill();
            c.clearRect(9, 5.5, 8, 3);
            c.beginPath(); c.moveTo(14, 3); c.lineTo(9, 7); c.lineTo(14, 11); c.closePath(); c.fill();
            break;
        case "speakers":
            rr(2, 2, 9, 20, 2); c.fill(); rr(13, 2, 9, 20, 2); c.fill();
            c.fillStyle = "#0b0a0a";
            c.beginPath(); c.arc(6.5, 15.5, 2.8, 0, 2 * Math.PI); c.fill();
            c.beginPath(); c.arc(17.5, 15.5, 2.8, 0, 2 * Math.PI); c.fill();
            c.beginPath(); c.arc(6.5, 7, 1.4, 0, 2 * Math.PI); c.fill();
            c.beginPath(); c.arc(17.5, 7, 1.4, 0, 2 * Math.PI); c.fill();
            break;
        case "headphones":
            c.lineWidth = 2.2;
            c.beginPath(); c.arc(12, 13, 8.5, Math.PI, 2 * Math.PI); c.stroke();
            rr(2.5, 12, 5, 9, 2); c.fill(); rr(16.5, 12, 5, 9, 2); c.fill();
            break;
        case "shuffle":
            c.lineWidth = 2;
            c.beginPath(); c.moveTo(2, 7); c.bezierCurveTo(10, 7, 12, 17, 19, 17); c.stroke();
            c.beginPath(); c.moveTo(2, 17); c.bezierCurveTo(10, 17, 12, 7, 19, 7); c.stroke();
            c.beginPath(); c.moveTo(18, 3); c.lineTo(23, 7); c.lineTo(18, 11); c.closePath(); c.fill();
            c.beginPath(); c.moveTo(18, 13); c.lineTo(23, 17); c.lineTo(18, 21); c.closePath(); c.fill();
            break;
        case "lowcut":
            c.lineWidth = 2;
            c.beginPath(); c.moveTo(3, 17); c.lineTo(8, 16); c.quadraticCurveTo(13, 14, 15, 9);
            c.lineTo(17, 7); c.lineTo(21, 7); c.stroke();
            break;
        case "clipguard":
            c.beginPath(); c.moveTo(12, 2); c.lineTo(21, 5); c.lineTo(20, 14); c.quadraticCurveTo(18, 19, 12, 22);
            c.quadraticCurveTo(6, 19, 4, 14); c.lineTo(3, 5); c.closePath(); c.stroke();
            c.beginPath(); c.moveTo(7.5, 9); c.lineTo(12, 16); c.lineTo(16.5, 9); c.stroke();
            break;
        case "autogain": {
            var h = [6, 14, 9, 17, 11, 15, 7];
            for (var i = 0; i < h.length; ++i) { rr(1.5 + i * 3.2, 11 - h[i] / 2, 1.8, h[i], 0.9); c.fill(); }
            c.lineWidth = 2;
            c.beginPath(); c.moveTo(8.5, 23); c.lineTo(12, 15); c.lineTo(15.5, 23); c.moveTo(9.7, 20.5); c.lineTo(14.3, 20.5); c.stroke();
            break; }
        case "laptop": {
            rr(3, 3, 18, 13, 1.5); c.stroke();
            var b = [4, 8, 5, 9, 6, 10, 5, 8, 4];
            for (var j = 0; j < b.length; ++j) { c.fillRect(5.2 + j * 1.8, 9.5 - b[j] / 2, 1, b[j]); }
            c.beginPath(); c.moveTo(1, 19.5); c.lineTo(23, 19.5); c.stroke();
            break; }
        }
    }
}
