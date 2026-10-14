import QtQuick

// First-run tour in the style of the original: dim everything except the section
// being explained (lime frame), big title + one-line explanation, Back / Next /
// Skip, four progress dots. Steps/wording are our own; sections follow the
// original (Inputs, Loopback, Outputs, Monitoring).
Item {
    id: root
    signal finished()
    property int step: 0
    readonly property var steps: [
        { t: "Inputs", s: "Connect a microphone or instrument", x: 0, w: 310 },
        { t: "Loopback", s: "Record your computer audio", x: 310, w: 85 },
        { t: "Outputs", s: "Control your hardware output loudness", x: 395, w: 170 },
        { t: "Monitoring", s: "Define how you want to monitor your signal", x: 565, w: 155 }]
    readonly property var cur: steps[step]
    readonly property int topY: 78
    readonly property int bottomY: 345
    MouseArea { anchors.fill: parent }   // modal
    // four dim rectangles around the highlighted section
    Rectangle { x: 0; y: 0; width: parent.width; height: root.topY; color: "#cc000000" }
    Rectangle { x: 0; y: root.bottomY; width: parent.width; height: parent.height - root.bottomY; color: "#cc000000" }
    Rectangle { x: 0; y: root.topY; width: root.cur.x; height: root.bottomY - root.topY; color: "#cc000000" }
    Rectangle { x: root.cur.x + root.cur.w; y: root.topY; width: parent.width - (root.cur.x + root.cur.w)
                height: root.bottomY - root.topY; color: "#cc000000" }
    Rectangle { x: root.cur.x + 1; y: root.topY + 1; width: root.cur.w - 2; height: root.bottomY - root.topY - 2
                color: "transparent"; border.color: Theme.accent; border.width: 3 }
    Column {
        x: root.cur.x + root.cur.w / 2 < 360 ? 330 : 14
        y: 104; spacing: 8; width: 236
        Text { text: root.cur.t; color: "#fff"; font.pixelSize: 26; font.bold: true }
        Text { width: 236; wrapMode: Text.WordWrap; text: root.cur.s; color: "#fff"; font.pixelSize: 14 }
        Row { spacing: 6; topPadding: 8
            ToggleButton { width: 58; height: 26; text: "Back"; active: true; visible: root.step > 0
                           onClicked: root.step -= 1 }
            ToggleButton { width: 58; height: 26; text: root.step === 3 ? "Done" : "Next"; primary: true
                           onClicked: { if (root.step === 3) root.finished(); else root.step += 1; } }
            ToggleButton { width: 58; height: 26; text: "Skip"; onClicked: root.finished() }
        }
    }
    Row {   // progress dots
        anchors.horizontalCenter: parent.horizontalCenter; y: root.bottomY + 9; spacing: 12
        Repeater { model: 4
            Rectangle { width: 12; height: 12; radius: 6; color: index <= root.step ? Theme.accent : "transparent"
                        border.color: Theme.accent; border.width: 1.5 } }
    }
}
