import QtQuick

// One input channel strip (guide items 1..15). index 1 = microphone, 2 = instrument.
Item {
    id: root
    property int index: 1
    property bool interactive: false
    readonly property string p: "in" + index + "."
    readonly property bool mic: index === 1
    function v(k, d) { var x = device.values[p + k]; return x === undefined ? d : x; }
    readonly property bool dsp: device.values["dsp.available"] !== false
    implicitWidth: 170; implicitHeight: 290

    LevelMeter { x: 0; y: 6; height: 240; scale: true; levelL: root.v("level", -60) }

    Column {
        x: 48; width: 116; spacing: 6
        Text { anchors.horizontalCenter: parent.horizontalCenter; color: Theme.text; font.pixelSize: 12
               text: root.mic ? "MICROPHONE" : "INSTRUMENT" }
        Row {
            spacing: 4; anchors.horizontalCenter: parent.horizontalCenter
            ToggleButton { width: 34; text: "LC"; tip: "Low-cut filter (remove rumble)"; enabled: root.interactive
                           active: root.v("highPass", false)
                           onClicked: device.setValue(root.p + "highPass", !active) }
            ToggleButton { width: 38; visible: root.mic; text: "48V"; tip: "Phantom power for condenser microphones"
                           enabled: root.interactive; active: root.v("phantom", false); activeColor: Theme.warn
                           onClicked: phantom.ask(!active) }
            ToggleButton { width: 34; text: "CG"; tip: "Clipguard: prevents input clipping"; enabled: root.interactive
                           active: root.v("clipguard", false)
                           onClicked: device.setValue(root.p + "clipguard", !active) }
        }
        GainKnob {
            anchors.horizontalCenter: parent.horizontalCenter
            enabled: root.interactive
            value: root.v("gain", 0); from: 0; to: root.v("gainMax", root.mic ? 72 : 60)
            minLabel: "0 dB"; maxLabel: to + " dB"
            onMoved: (nv) => device.setValue(root.p + "gain", nv)
        }
        ToggleButton { anchors.horizontalCenter: parent.horizontalCenter; width: 70; text: "Autogain"
                       tip: "Measure and set the input gain (10 s performance)"
                       enabled: root.interactive; onClicked: device.trigger("autogain" + root.index) }
        Row {
            spacing: 2; anchors.horizontalCenter: parent.horizontalCenter
            Repeater {
                model: ["Clean", "Warm", "Vivid"]
                ToggleButton { width: 38; text: modelData; enabled: root.interactive
                               active: root.v("preamp", 0) === index
                               onClicked: device.setValue(root.p + "preamp", index) }
            }
        }
        Row {
            spacing: 4; anchors.horizontalCenter: parent.horizontalCenter
            ToggleButton { width: 56; text: "Denoise"; enabled: root.interactive; unavailable: !root.dsp
                           tip: root.dsp ? "Remove background noise" : "Not available at 96 kHz"
                           active: root.v("denoise", false)
                           onClicked: device.setValue(root.p + "denoise", !active) }
            ToggleButton { width: 56; text: "Comp"; enabled: root.interactive; unavailable: !root.dsp
                           tip: root.dsp ? "Compressor" : "Not available at 96 kHz"
                           active: root.v("compress", false)
                           onClicked: device.setValue(root.p + "compress", !active) }
        }
        ToggleButton { anchors.horizontalCenter: parent.horizontalCenter; width: 70; text: "MUTE"
                       activeColor: Theme.bad; enabled: root.interactive; active: root.v("mute", false)
                       onClicked: device.setValue(root.p + "mute", !active) }
    }

    // 48 V safety confirmation (always before enabling).
    Rectangle {
        id: phantom
        visible: false; z: 20; anchors.centerIn: parent; width: 150; height: 110; radius: 6
        color: Theme.panelHi; border.color: Theme.warn
        function ask(on) { if (!on) device.setValue(root.p + "phantom", false); else visible = true; }
        Column { anchors.centerIn: parent; spacing: 8
            Text { width: 130; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter
                   color: Theme.text; font.pixelSize: 11
                   text: "Enable 48 V phantom power? Disconnect ribbon microphones first." }
            Row { spacing: 8; anchors.horizontalCenter: parent.horizontalCenter
                ToggleButton { width: 56; text: "Cancel"; onClicked: phantom.visible = false }
                ToggleButton { width: 56; text: "Enable"; active: true; activeColor: Theme.warn
                               onClicked: { phantom.visible = false; device.setValue(root.p + "phantom", true); } } }
        }
    }
}
