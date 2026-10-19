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
    readonly property bool mic: index === 1
    function v(k, d) { var x = device.values[p + k]; return x === undefined ? d : x; }
    readonly property bool dsp: device.values["dsp.available"] !== false
    readonly property bool pending: device.values["pending." + p + "gain"] === true
    width: 152; height: 267
    Rectangle { anchors.fill: parent; anchors.margins: 2; radius: 3; color: "transparent"
                border.width: root.physicalSelected ? 2 : 0; border.color: "#75b9ff" }

    LevelMeter { x: -9; y: 40; height: 187; scale: true; barWidth: 8
                 stageScale: root.stageScale
                 objectName: root.mic ? "meterMic" : "meterInstrument"
                 meterL: device.meters.channel(root.mic ? 0 : 1)
                 sourceFreshness: device.meters.freshness
                 levelL: root.v("level", -60) }

    GlyphIcon { x: 90 - 12; y: 8; width: 24; height: 24; kind: root.mic ? "mic" : "guitar"; color: "#ececec" }

    Row {   // LC / 48V / Clipguard (instrument has no 48V)
        x: root.mic ? 45 : 60; y: 45; spacing: 4
        ToggleButton { width: 27; height: 19; icon: "lowcut"; tip: "Remove low frequency rumble"
                       enabled: root.interactive; active: root.v("highPass", false)
                       onClicked: device.setValue(root.p + "highPass", !active) }
        ToggleButton { width: 27; height: 19; visible: root.mic; text: "48V"; fontSize: 9
                       tip: "Provide power for your condenser microphone"
                       enabled: root.interactive; active: root.v("phantom", false)
                       onClicked: phantom.ask(!active) }
        ToggleButton { width: 27; height: 19; icon: "clipguard"; tip: "Activate Clipguard to prevent your input from distorting"
                       enabled: root.interactive; active: root.v("clipguard", false)
                       onClicked: device.setValue(root.p + "clipguard", !active) }
    }

    GainKnob {
        x: 90 - width / 2; y: 78
        enabled: root.interactive
        tip: "Adjust the input gain"
        value: root.v("gain", 0); from: 0; to: root.v("gainMax", root.mic ? 72 : 60)
        minLabel: "0 dB"; maxLabel: to + " dB"
        onMoved: (nv) => device.setValue(root.p + "gain", nv)
    }
    // pending-edit marker: a small dot while a write is in flight / unconfirmed
    Rectangle { visible: root.pending; x: 128; y: 84; width: 6; height: 6; radius: 3; color: Theme.warn }

    ToggleButton { x: 29; y: 147; width: 24; height: 24; text: "A"; fontSize: 15; flat: true
                   tip: "Automatically set up your input gain"
                   enabled: root.interactive; onClicked: device.trigger("autogain" + root.index) }

    Row {
        x: 32; y: 180; spacing: 1
        Repeater {
            model: ["Clean", "Warm", "Vivid"]
            ToggleButton { width: 39; height: 20; text: modelData; enabled: root.interactive; tip: "Choose a sound for your preamp"
                           active: root.v("preamp", 0) === index
                           onClicked: device.setValue(root.p + "preamp", index) }
        }
    }
    Row {
        x: 31; y: 211; spacing: 4
        ToggleButton { width: 57; height: 20; text: "Denoise"; enabled: root.interactive; unavailable: !root.dsp
                       tip: root.dsp ? "Good for live streaming" : "Choose lower sample rate to enable this function"
                       active: root.v("denoise", false)
                       onClicked: device.setValue(root.p + "denoise", !active) }
        ToggleButton { width: 57; height: 20; text: "Comp"; enabled: root.interactive; unavailable: !root.dsp
                       tip: root.dsp ? "Consistent level, louder signal, finished sound" : "Choose lower sample rate to enable this function"
                       active: root.v("compress", false)
                       onClicked: device.setValue(root.p + "compress", !active) }
    }
    ToggleButton { x: 58; y: 240; width: 64; height: 24; text: "MUTE"; danger: true; tip: "Mute the signal"
                   enabled: root.interactive; active: root.v("mute", false)
                   onClicked: device.setValue(root.p + "mute", !active) }

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
