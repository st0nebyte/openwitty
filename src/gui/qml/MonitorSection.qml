import QtQuick

// Monitoring: Input/playback mix, direct input selection, Dim, Mono.
Item {
    id: root
    property bool interactive: false
    function v(k, d) { var x = device.values[k]; return x === undefined ? d : x; }
    implicitWidth: 180; implicitHeight: 290
    Text { anchors.horizontalCenter: parent.horizontalCenter; color: Theme.text; font.pixelSize: 12; text: "Input / playback" }
    GainKnob {
        y: 24; anchors.horizontalCenter: parent.horizontalCenter
        enabled: root.interactive; showValue: false
        from: -1; to: 1; step: 0.05
        value: root.v("mon.mix", 0)
        minLabel: "100:0"; maxLabel: "0:100"
        onMoved: (nv) => device.setValue("mon.mix", nv)
    }
    Text { x: 10; y: 140; color: Theme.textDim; font.pixelSize: 10; text: "Direct\ninput signal" }
    Text { anchors.right: parent.right; anchors.rightMargin: 10; y: 140; horizontalAlignment: Text.AlignRight
           color: Theme.textDim; font.pixelSize: 10; text: "Computer\naudio" }
    Row {
        y: 180; x: 10; spacing: 4
        ToggleButton { width: 40; text: "Mic"; enabled: root.interactive; active: root.v("mon.mic", false)
                       tip: "Monitor the microphone (at least one source stays selected)"
                       onClicked: device.setValue("mon.mic", !active) }
        ToggleButton { width: 40; text: "Inst"; enabled: root.interactive; active: root.v("mon.inst", false)
                       tip: "Monitor the instrument"
                       onClicked: device.setValue("mon.inst", !active) }
    }
    Row {
        y: 258; anchors.horizontalCenter: parent.horizontalCenter; spacing: 6
        ToggleButton { width: 70; text: "Dim"; enabled: root.interactive; active: root.v("out.dim", false)
                       tip: "Lower speaker and headphone volume"
                       onClicked: device.setValue("out.dim", !active) }
        ToggleButton { width: 70; text: "Mono"; enabled: root.interactive; active: root.v("out.mono", false)
                       tip: "Listen in mono"
                       onClicked: device.setValue("out.mono", !active) }
    }
}
