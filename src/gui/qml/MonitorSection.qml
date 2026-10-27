import QtQuick

// Monitoring column at the reference's measured coordinates (section 155 x 267).
Item {
    id: root
    property bool interactive: false
    function v(k, d) { var x = device.values[k]; return x === undefined ? d : x; }
    // Phase 10A: a control is enabled only if the backend says its exact bytes/trigger are authorized.
    function can(k) { return root.interactive && device.values["can." + k] === true; }
    width: 155; height: 267
    GlyphIcon { x: 77 - 15; y: 8; width: 30; height: 30; kind: "shuffle"; color: "#f0f0f0" }
    Text { anchors.horizontalCenter: parent.horizontalCenter; y: 47; color: Theme.textDim
           font.pixelSize: 10; text: "Input / playback" }
    GainKnob {
        objectName: "monitorMixKnob"
        x: 77 - width / 2; y: 123 - height / 2 + 0; bodyRadius: 41; sweepDeg: 300; labelInset: 6
        enabled: root.can("mon.mix") && device.values["inflight.mon.mix"] !== true; showValue: false; warnFrom: 1
        commitOnRelease: !device.simulated
        tip: device.simulated ? "Hear your input signal and computer"
             : "Monitoring mix of the CONNECT 2 (device scale -100...0 between the physical stops). "
               + "How the values in between map to the 100:0...0:100 balance is not calibrated."
        // Device scale -100..0 (physical stops observed in Phase 7C), also used by the simulator since Phase 11A.
        from: -100; to: 0
        step: 1
        value: root.v("mon.mix", 0)
        minLabel: "100:0"; maxLabel: "0:100"
        onMoved: (nv) => device.setValue("mon.mix", nv)
        onCommitted: (nv) => device.setValue("mon.mix", nv)
    }
    // Phase 10B: on the real device the raw value is shown, since the label mapping is not calibrated.
    Text { objectName: "monitorMixReadout"; visible: !device.simulated && device.values["mon.mix"] !== undefined
           anchors.horizontalCenter: parent.horizontalCenter; y: 168; color: Theme.textDim; font.pixelSize: 9
           text: "device value " + Math.round(root.v("mon.mix", 0)) + " (of -100...0)" }
    Text { x: 3; y: 186; horizontalAlignment: Text.AlignHCenter; width: 78; color: Theme.textDim
           font.pixelSize: 9; text: "Direct\ninput signal" }
    Text { x: 80; y: 186; horizontalAlignment: Text.AlignHCenter; width: 70; color: Theme.textDim
           font.pixelSize: 9; text: "Computer\naudio" }
    Row {
        x: 21; y: 210; spacing: 3
        ToggleButton { width: 23; height: 21; icon: "mic"; enabled: root.can("mon.mic")
                       active: root.v("mon.mic", false)
                       tip: "Activate to listen to the direct input signal"
                       onClicked: device.setValue("mon.mic", !active) }
        ToggleButton { width: 23; height: 21; icon: "guitar"; enabled: root.can("mon.inst")
                       active: root.v("mon.inst", false)
                       tip: "Activate to listen to the direct input signal"
                       onClicked: device.setValue("mon.inst", !active) }
    }
    GlyphIcon { x: 101; y: 207; width: 32; height: 26; kind: "laptop"; color: "#f0f0f0" }
    ToggleButton { objectName: "dimButton"; x: 22; y: 241; width: 49; height: 23; text: "Dim"; enabled: root.can("out.dim")
                   active: root.v("out.dim", false)
                   tip: "Lower the speaker and headphone output volume"
                   onClicked: device.setValue("out.dim", !active) }
    ToggleButton { x: 78; y: 241; width: 50; height: 23; text: "Mono"; enabled: root.can("out.mono")
                   active: root.v("out.mono", false)
                   tip: "Listen to your signals in mono"
                   onClicked: device.setValue("out.mono", !active) }
}
