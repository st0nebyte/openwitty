import QtQuick

// Vertical dB meter: dark well, olive bar that turns amber toward the top, scale
// labels (0 … -54, -∞) on the left like the original. Pure bindings, no timers.
Item {
    id: root
    property real levelL: -60
    property real levelR: -60
    property bool stereo: false
    property bool scale: false
    property real minDb: -60
    property int barWidth: 8
    implicitWidth: (scale ? 26 : 0) + (stereo ? 2 * barWidth + 2 : barWidth); implicitHeight: 187
    function frac(v) { return Math.max(0, Math.min(1, (v - minDb) / -minDb)); }

    Repeater {   // dB labels
        model: root.scale ? [0, -6, -12, -18, -24, -30, -36, -42, -48, -54, "-∞"] : []
        Text {
            x: 0; width: 22; horizontalAlignment: Text.AlignRight
            y: (index / 10) * (root.height - 8) - 1
            text: modelData; color: Theme.textDim; font.pixelSize: 8
        }
    }
    Row {
        x: root.scale ? 26 : 0; spacing: 2; height: parent.height
        Repeater {
            model: root.stereo ? 2 : 1
            Rectangle {
                width: root.barWidth; height: root.height; color: Theme.well
                property real v: index === 0 ? root.levelL : (root.stereo ? root.levelR : root.levelL)
                Rectangle {   // green part (up to -12 dB)
                    anchors.bottom: parent.bottom; width: parent.width
                    height: parent.height * root.frac(Math.min(parent.v, -12))
                    color: Theme.meterGreen
                }
                Rectangle {   // amber part (above -12 dB)
                    visible: parent.v > -12
                    y: parent.height * (1 - root.frac(parent.v)); width: parent.width
                    height: parent.height * (root.frac(parent.v) - root.frac(-12))
                    color: Theme.meterAmber
                }
            }
        }
    }
}
