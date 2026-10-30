import QtQuick
import QtQuick.Window
import QtQuick.Shapes

// Vertical dB meter: dark well, olive bar that turns amber toward the top, scale
// labels (0 … -54, -∞) on the left. A shared FrameAnimation advances presentation
// state; this component never initiates transport I/O or fabricates samples.
Item {
    id: root
    property real levelL: -60
    property real levelR: -60
    property var meterL: null
    property var meterR: null
    property string sourceFreshness: "fresh"
    property bool stereo: false
    property bool scale: false
    property real minDb: -60
    // Measured noise floor (dBFS) shown as a blue line; NaN = none. Below the scale it sits at the bottom edge.
    property real noiseFloorDb: NaN
    readonly property bool hasNoiseFloor: !isNaN(noiseFloorDb)
    property bool noiseTipShown: false   // the parent raises itself while the tooltip is open (it overlaps neighbours)
    property int barWidth: 8
    property real stageScale: 1
    readonly property real pixelRatio: root.Window.window ? root.Window.window.devicePixelRatio : Screen.devicePixelRatio
    readonly property real pixelsPerUnit: Math.max(0.01, stageScale * pixelRatio)
    implicitWidth: (scale ? 26 : 0) + (stereo ? 2 * barWidth + 2 : barWidth); implicitHeight: 187
    function frac(v) { return Math.max(0, Math.min(1, (v - minDb) / -minDb)); }
    function snappedPeakY(bar, db) {
        // Snap in screen-pixel space, including the stage's fractional offset.
        // Only the peak indicator is snapped; the moving level keeps its envelope.
        var top = bar.mapToItem(null, 0, 0).y;
        var ideal = bar.height * (1 - frac(db));
        var physical = Math.round((top + ideal * stageScale) * pixelRatio);
        var local = (physical / pixelRatio - top) / stageScale;
        return Math.max(0, Math.min(bar.height - 1 / pixelsPerUnit, local));
    }

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
                id: bar
                property int channelIndex: index
                objectName: "meterBar" + channelIndex
                width: root.barWidth; height: root.height; color: Theme.well
                property var sample: index === 0 ? root.meterL : root.meterR
                property bool sampleValid: sample ? sample.valid : true
                property bool usable: sampleValid && (root.sourceFreshness === "fresh" || root.sourceFreshness === "delayed")
                property real v: usable ? (sample ? sample.displayDb : (index === 0 ? root.levelL : root.levelR)) : root.minDb
                opacity: root.sourceFreshness === "delayed" ? 0.45 : 1
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
                Rectangle {   // measured noise floor (Denoise measurement)
                    objectName: "noiseFloorMarker" + bar.channelIndex
                    visible: root.hasNoiseFloor
                    x: -2; width: parent.width + 4; height: Math.max(1.5, 1.5 / root.pixelsPerUnit)
                    y: Math.min(parent.height - height, parent.height * (1 - root.frac(root.noiseFloorDb)) - height / 2)
                    color: "#75b9ff"
                    MouseArea { id: nfHover; anchors.fill: parent; anchors.margins: -3; hoverEnabled: true
                                onContainsMouseChanged: root.noiseTipShown = containsMouse }
                    ToolTip_ { visible: nfHover.containsMouse && Hint.enabled; x: 12
                               text: "Measured noise floor: " + Math.round(root.noiseFloorDb) + " dB"
                                     + (root.noiseFloorDb < root.minDb ? " (below the scale)" : "")
                                     + "\nfrom the last Denoise measurement" }
                }
                Shape {   // sampled peak hold; not an unobserved inter-sample peak
                    id: peak
                    objectName: "sampledPeakMarker" + bar.channelIndex
                    // A marker overlapping the live bar looks like a duplicate
                    // row. Show it only when a held observed peak is separated
                    // from the current envelope by at least one screen pixel.
                    visible: parent.usable && parent.sample !== null
                             && (root.frac(parent.sample.peakDb) - root.frac(parent.v))
                                * parent.height * root.pixelsPerUnit >= 1
                    x: 0; width: parent.width; height: 1 / root.pixelsPerUnit
                    y: root.snappedPeakY(parent, parent.sample ? parent.sample.peakDb : root.minDb)
                    ShapePath {
                        strokeColor: Theme.meterAmber
                        strokeWidth: 1 / root.pixelsPerUnit
                        fillColor: "transparent"
                        startX: 0; startY: peak.height / 2
                        PathLine { x: peak.width; y: peak.height / 2 }
                    }
                }
            }
        }
    }
}
