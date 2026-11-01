import QtQuick

// Loopback (meters + mute) / Loudspeaker / Headphone (meters + fader + mute).
Item {
    id: root
    property string group: "spk"       // loop | spk | hp
    property bool interactive: false
    property bool physicalSelected: false
    property real stageScale: 1
    function v(k, d) { var x = device.values[root.group + "." + k]; return x === undefined ? d : x; }
    // Phase 10A: a control is enabled only if the backend says its exact bytes/trigger are authorized.
    function can(k) { return root.interactive && device.values["can." + k] === true; }
    readonly property bool hasFader: group !== "loop"
    readonly property bool pending: device.values["pending." + root.group + ".volume"] === true
                                    || device.values["pending." + root.group + ".mute"] === true
    width: 85; height: 267
    Rectangle { visible: root.pending; x: 74; y: 8; width: 6; height: 6; radius: 3; color: Theme.warn }

    GlyphIcon { x: 42.5 - width / 2; y: 8; width: 30; height: 30
                kind: group === "loop" ? "loop" : group === "spk" ? "speakers" : "headphones"
                objectName: "channelIcon_" + root.group
                highlighted: root.physicalSelected; color: "#f0f0f0" }
    LevelMeter { x: group === "loop" ? 7 : 5; y: 44; height: 187; stereo: true; scale: true
                 stageScale: root.stageScale
                 objectName: root.group === "loop" ? "meterLoopback" : root.group === "spk" ? "meterSpeaker" : "meterHeadphone"
                 meterL: device.meters.channel(root.group === "loop" ? 2 : root.group === "spk" ? 4 : 6)
                 meterR: device.meters.channel(root.group === "loop" ? 3 : root.group === "spk" ? 5 : 7)
                 sourceFreshness: device.meters.freshness
                 levelL: root.v("levelL", -60); levelR: root.v("levelR", -60) }
    Fader { id: fader; objectName: "fader_" + root.group; visible: root.hasFader; x: 55; y: 44; height: 187; enabled: root.can(root.group + ".volume") && device.values["inflight." + root.group + ".volume"] !== true
            commitOnRelease: !device.simulated
            tip: (root.group === "spk" ? "Adjust the loudspeaker volume" : "Adjust the headphone volume") + " (hold Shift for 0.1 dB steps)"
            value: root.v("volume", 0)
            onMoved: (nv) => device.setValue(root.group + ".volume", nv)
            onCommitted: (nv) => device.setValue(root.group + ".volume", nv) }
    // dB readout of the fader (Phase 11B session 2: the value was not visible). Lime while a change is previewed or
    // waiting for the device; the device's own value otherwise.
    Column {
        visible: root.hasFader; x: 58; y: 12; width: 26
        readonly property bool previewing: fader.commitOnRelease && (fader.previewing || fader.awaiting)
        Text { objectName: "faderValue_" + root.group; width: parent.width; horizontalAlignment: Text.AlignRight
               font.pixelSize: 9; font.bold: true; color: parent.previewing ? "#a4c400" : Theme.textDim
               text: fader.shownValue <= fader.from ? "-\u221e" : (Math.round(fader.shownValue * 10) / 10).toFixed(1) }
        Text { width: parent.width; horizontalAlignment: Text.AlignRight; font.pixelSize: 8; color: Theme.textDim; text: "dB" }
    }
    ToggleButton { objectName: "mute_" + root.group; x: 18; y: 240; width: 50; height: 24; text: "MUTE"; danger: true; tip: "Mute the signal"
                   enabled: root.can(root.group + ".mute"); active: root.v("mute", false)
                   onClicked: device.setValue(root.group + ".mute", !active) }
}
