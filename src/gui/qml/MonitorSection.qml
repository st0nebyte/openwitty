import QtQuick

// Monitoring column at the reference's measured coordinates (section 155 x 267).
Item {
    id: root
    property bool interactive: false
    function v(k, d) { var x = device.values[k]; return x === undefined ? d : x; }
    width: 155; height: 267
    GlyphIcon { x: 77 - 15; y: 8; width: 30; height: 30; kind: "shuffle"; color: "#f0f0f0" }
    Text { anchors.horizontalCenter: parent.horizontalCenter; y: 47; color: Theme.textDim
           font.pixelSize: 10; text: "Input / playback" }
    GainKnob {
        x: 77 - width / 2; y: 123 - height / 2 + 0; bodyRadius: 41; sweepDeg: 300; labelInset: 6
        enabled: root.interactive; showValue: false; warnFrom: 1
        tip: "Hear your input signal and computer"
        from: -1; to: 1; step: 0.05
        value: root.v("mon.mix", 0)
        minLabel: "100:0"; maxLabel: "0:100"
        onMoved: (nv) => device.setValue("mon.mix", nv)
    }
    Text { x: 3; y: 186; horizontalAlignment: Text.AlignHCenter; width: 78; color: Theme.textDim
           font.pixelSize: 9; text: "Direct\ninput signal" }
    Text { x: 80; y: 186; horizontalAlignment: Text.AlignHCenter; width: 70; color: Theme.textDim
           font.pixelSize: 9; text: "Computer\naudio" }
    Row {
        x: 21; y: 210; spacing: 3
        ToggleButton { width: 23; height: 21; icon: "mic"; enabled: root.interactive
                       active: root.v("mon.mic", false)
                       tip: "Activate to listen to the direct input signal"
                       onClicked: device.setValue("mon.mic", !active) }
        ToggleButton { width: 23; height: 21; icon: "guitar"; enabled: root.interactive
                       active: root.v("mon.inst", false)
                       tip: "Activate to listen to the direct input signal"
                       onClicked: device.setValue("mon.inst", !active) }
    }
    GlyphIcon { x: 101; y: 207; width: 32; height: 26; kind: "laptop"; color: "#f0f0f0" }
    ToggleButton { x: 22; y: 241; width: 49; height: 23; text: "Dim"; enabled: root.interactive
                   active: root.v("out.dim", false)
                   tip: "Lower the speaker and headphone output volume"
                   onClicked: device.setValue("out.dim", !active) }
    ToggleButton { x: 78; y: 241; width: 50; height: 23; text: "Mono"; enabled: root.interactive
                   active: root.v("out.mono", false)
                   tip: "Listen to your signals in mono"
                   onClicked: device.setValue("out.mono", !active) }
}
