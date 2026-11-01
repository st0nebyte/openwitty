import QtQuick

// One input channel strip, laid out in the original's coordinates (strip 152 x 267).
// index 1 = microphone, 2 = instrument.
Item {
    id: root
    property int index: 1
    property bool interactive: false
    property bool physicalSelected: false
    property real stageScale: 1
    readonly property string p: "in" + index + "."
    // Denoise level the device stored at its last measurement (bytes 8/17); 0 = never measured
    readonly property bool hasNoiseLevel: { var n = device.values[p + "noiseFloor"];
                                            return typeof n === "number" && n < 0 && n >= -100; }
    readonly property bool canMeasureDenoise: !device.simulated && can("denoise" + index)
    readonly property bool mic: index === 1
    function v(k, d) { var x = device.values[p + k]; return x === undefined ? d : x; }
    // Phase 10A: a control is enabled only if the backend says its exact bytes/trigger are authorized.
    function can(k) { return root.interactive && device.values["can." + k] === true; }
    // Phase 10B: Denoise/Comp at 96 kHz. "unknown" (real device, no confirmed rate) is not shown as unavailable.
    readonly property bool dsp: device.values["dsp.state"] !== "unavailable"
    readonly property bool dspUnknown: device.values["dsp.state"] === "unknown"
    readonly property bool pending: device.values["pending." + p + "gain"] === true
    width: 152; height: 267

    z: inputMeter.noiseTipShown ? 10 : 0
    LevelMeter { id: inputMeter; x: -9; y: 40; height: 187; scale: true; barWidth: 8
                 stageScale: root.stageScale
                 objectName: root.mic ? "meterMic" : "meterInstrument"
                 meterL: device.meters.channel(root.mic ? 0 : 1)
                 sourceFreshness: device.meters.freshness
                 levelL: root.v("level", -60)
                 // the device's last Denoise measurement (bytes 8/17); 0 = never measured, nothing shown
                 // shown only while Denoise is on (owner wish 2026-10-31)
                 noiseFloorDb: (root.hasNoiseLevel && root.v("denoise", false)) ? device.values[root.p + "noiseFloor"] : NaN }

    GlyphIcon { x: 90 - 12; y: 8; width: 24; height: 24; kind: root.mic ? "mic" : "guitar"
                objectName: root.mic ? "channelIconMic" : "channelIconInstrument"
                // the channel the device's touch wheel controls: icon in the selection colour (owner wish)
                highlighted: root.physicalSelected; color: "#ececec" }

    Row {   // LC / 48V / Clipguard (instrument has no 48V)
        x: root.mic ? 45 : 60; y: 45; spacing: 4
        ToggleButton { width: 27; height: 19; icon: "lowcut"; tip: "Remove low frequency rumble"
                       enabled: root.can(root.p + "highPass"); active: root.v("highPass", false)
                       onClicked: device.setValue(root.p + "highPass", !active) }
        ToggleButton { objectName: "phantomButton" + root.index; width: 27; height: 19; visible: root.mic; text: "48V"; fontSize: 9
                       tip: "Provide power for your condenser microphone"
                       enabled: root.can(root.p + "phantom"); active: root.v("phantom", false)
                       onClicked: phantom.ask(!active) }
        ToggleButton { width: 27; height: 19; icon: "clipguard"; tip: "Activate Clipguard to prevent your input from distorting"
                       enabled: root.can(root.p + "clipguard"); active: root.v("clipguard", false)
                       onClicked: device.setValue(root.p + "clipguard", !active) }
    }

    GainKnob {
        objectName: "gainKnob" + root.index
        x: 90 - width / 2; y: 78
        enabled: root.can(root.p + "gain") && device.values["inflight." + root.p + "gain"] !== true
        commitOnRelease: !device.simulated
        tip: "Adjust the input gain"
        value: root.v("gain", 0); from: 0; to: root.v("gainMax", root.mic ? 72 : 60)
        minLabel: "0 dB"; maxLabel: to + " dB"
        onMoved: (nv) => device.setValue(root.p + "gain", nv)
        onCommitted: (nv) => device.setValue(root.p + "gain", nv)
    }
    // pending-edit marker: a small dot while a write is in flight / unconfirmed
    Rectangle { visible: root.pending; x: 128; y: 84; width: 6; height: 6; radius: 3; color: Theme.warn }

    ToggleButton { objectName: "autogainButton" + root.index; x: 29; y: 147; width: 24; height: 24; text: "A"; fontSize: 15; flat: true
                   tip: "Automatically set up your input gain"
                   enabled: root.can("autogain" + root.index)
                   // simulated device: start at once; real device: confirm every measurement (the device sets the gain itself)
                   onClicked: { if (device.simulated) device.trigger("autogain" + root.index); else autogainConfirm.visible = true; } }

    Row {
        x: 32; y: 180; spacing: 1
        Repeater {
            model: ["Clean", "Warm", "Vivid"]
            ToggleButton { width: 39; height: 20; text: modelData; enabled: root.can(root.p + "preamp"); tip: "Choose a sound for your preamp"
                           active: root.v("preamp", 0) === index
                           onClicked: device.setValue(root.p + "preamp", index) }
        }
    }
    Row {
        x: 31; y: 211; spacing: 4
        ToggleButton { id: denoiseButton; objectName: "denoiseButton" + root.index
                       width: 57; height: 20; text: "Denoise"; enabled: root.can(root.p + "denoise"); unavailable: !root.dsp
                       tip: !root.dsp ? "Choose lower sample rate to enable this function"
                            : "Good for live streaming"
                              + (root.canMeasureDenoise ? " (right-click: measure again)" : "")
                       active: root.v("denoise", false)
                       // The device keeps the measured level (bytes 8/17) while Denoise is off, so switching on reuses it
                       // (owner wish 2026-10-31); the measurement dialog only comes up when no level was ever measured.
                       // Right-click measures again. Switching off is always a plain write.
                       onClicked: { if (!active && root.canMeasureDenoise && !root.hasNoiseLevel) denoiseConfirm.visible = true;
                                    else device.setValue(root.p + "denoise", !active); }
                       MouseArea { anchors.fill: parent; acceptedButtons: Qt.RightButton
                                   enabled: root.canMeasureDenoise && !denoiseButton.unavailable
                                   onClicked: denoiseConfirm.visible = true } }
        ToggleButton { width: 57; height: 20; text: "Comp"; enabled: root.can(root.p + "compress"); unavailable: !root.dsp
                       tip: root.dsp ? "Consistent level, louder signal, finished sound" : "Choose lower sample rate to enable this function"
                       active: root.v("compress", false)
                       onClicked: device.setValue(root.p + "compress", !active) }
    }
    ToggleButton { objectName: "inputMute" + root.index; x: 58; y: 240; width: 64; height: 24; text: "MUTE"; danger: true; tip: "Mute the signal"
                   enabled: root.can(root.p + "mute"); active: root.v("mute", false)
                   onClicked: device.setValue(root.p + "mute", !active) }

    Rectangle {   // Denoise: measure the background noise first (every time, real device)
        id: denoiseConfirm
        objectName: "denoiseConfirm" + root.index
        visible: false; z: 20; anchors.centerIn: parent; width: 160; height: 132; radius: 5
        color: Theme.panelHi; border.color: Theme.warn
        Column { anchors.centerIn: parent; spacing: 6
            Text { width: 144; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter
                   color: Theme.text; font.pixelSize: 10
                   text: "Denoise measures the background noise: stay quiet for a few seconds after Measure." }
            ToggleButton { objectName: "denoiseMeasure" + root.index; width: 144; text: "Measure"; active: true
                           activeColor: Theme.warn
                           onClicked: { denoiseConfirm.visible = false; device.trigger("denoise" + root.index); } }
            Row { spacing: 6; anchors.horizontalCenter: parent.horizontalCenter
                ToggleButton { objectName: "denoiseOnOnly" + root.index; width: 69; text: "Only switch on"
                               onClicked: { denoiseConfirm.visible = false; device.setValue(root.p + "denoise", true); } }
                ToggleButton { width: 69; text: "Cancel"; onClicked: denoiseConfirm.visible = false } }
        }
    }

    Rectangle {   // autogain confirmation (every measurement on the real device)
        id: autogainConfirm
        objectName: "autogainConfirm" + root.index
        visible: false; z: 20; anchors.centerIn: parent; width: 150; height: 118; radius: 5
        color: Theme.panelHi; border.color: Theme.warn
        Column { anchors.centerIn: parent; spacing: 8
            Text { width: 134; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter
                   color: Theme.text; font.pixelSize: 10
                   text: "Start autogain? The device sets the gain by itself and may raise it. Turn monitoring and "
                       + "speakers down, then play or speak at your normal level." }
            Row { spacing: 8; anchors.horizontalCenter: parent.horizontalCenter
                ToggleButton { width: 56; text: "Cancel"; onClicked: autogainConfirm.visible = false }
                ToggleButton { objectName: "autogainStart" + root.index; width: 56; text: "Start"; active: true
                               activeColor: Theme.warn
                               onClicked: { autogainConfirm.visible = false; device.trigger("autogain" + root.index); } } }
        }
    }

    Rectangle {   // 48 V safety confirmation (always before enabling)
        id: phantom
        visible: false; z: 20; anchors.centerIn: parent; width: 140; height: 100; radius: 5
        color: Theme.panelHi; border.color: Theme.warn
        function ask(on) { if (!on) device.setValue(root.p + "phantom", false); else visible = true; }
        Column { anchors.centerIn: parent; spacing: 8
            Text { width: 124; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter
                   color: Theme.text; font.pixelSize: 10
                   text: "Enable 48 V phantom power? Disconnect ribbon microphones first." }
            Row { spacing: 8; anchors.horizontalCenter: parent.horizontalCenter
                ToggleButton { width: 52; text: "Cancel"; onClicked: phantom.visible = false }
                ToggleButton { width: 52; text: "Enable"; active: true; activeColor: Theme.warn
                               onClicked: { phantom.visible = false; device.setValue(root.p + "phantom", true); } } }
        }
    }
}
