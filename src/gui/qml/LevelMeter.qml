import QtQuick

// Vertical dB meter, one or two bars. Scale -60..0 dB; redraws only when the
// value changes (bound rectangle heights, no timers).
Item {
    id: root
    property real levelL: -60
    property real levelR: -60
    property bool stereo: false
    property bool scale: false
    property real minDb: -60
    implicitWidth: (stereo ? 22 : 12) + (scale ? 22 : 0); implicitHeight: 160
    function frac(v) { return Math.max(0, Math.min(1, (v - minDb) / -minDb)); }
    function col(v) { return v > -3 ? Theme.bad : v > -12 ? Theme.warn : Theme.ok; }

    Column {   // dB labels
        visible: root.scale
        width: 20; height: parent.height
        Repeater {
            model: [0, -6, -12, -18, -24, -30, -36, -42, -48, -54]
            Item { width: 20; height: root.height / 10
                   Text { anchors.right: parent.right; text: modelData; color: Theme.textDim; font.pixelSize: 8 } }
        }
    }
    Row {
        x: root.scale ? 22 : 0; spacing: 2; height: parent.height
        Repeater {
            model: root.stereo ? 2 : 1
            Rectangle {
                width: 10; height: root.height; color: Theme.well; border.color: Theme.line
                property real v: index === 0 ? root.levelL : (root.stereo ? root.levelR : root.levelL)
                Rectangle {
                    anchors.bottom: parent.bottom; anchors.margins: 1; width: parent.width - 2
                    height: (parent.height - 2) * root.frac(parent.v)
                    color: root.col(parent.v)
                }
            }
        }
    }
}
