import QtQuick

// Reconstruction of the observed Autosetup workflow (target -> +48 V -> preamp
// character -> Denoise -> Clipguard -> timed measurement -> summary). The
// individual choices are applied through ordinary settings writes; the
// measurement uses the auto-gain trigger. Not protocol-verified.
Rectangle {
    id: root
    property bool interactive: false
    signal closeRequested()
    color: "#000000cc"
    property int step: 0
    property string target: "both"       // mic | inst | both
    property bool phantom: false
    property int preamp: 0
    property bool denoise: false
    property bool clipguard: true
    readonly property bool dsp: device.values["dsp.available"] !== false
    readonly property string autoState: device.values["auto.state"] || "idle"
    readonly property int progress: device.values["auto.progress"] || 0
    MouseArea { anchors.fill: parent }   // modal

    function apply() {
        var ins = target === "mic" ? [1] : target === "inst" ? [2] : [1, 2];
        for (var i = 0; i < ins.length; ++i) {
            var p = "in" + ins[i] + ".";
            device.setValue(p + "preamp", preamp);
            device.setValue(p + "clipguard", clipguard);
            if (dsp) device.setValue(p + "denoise", denoise);
        }
        if (phantom && ins.indexOf(1) >= 0) device.setValue("in1.phantom", true);
    }
    onAutoStateChanged: if (step === 5 && autoState === "finished") step = 6

    Rectangle {
        anchors.centerIn: parent; width: 420; height: 280; radius: 8; color: Theme.panel; border.color: Theme.line
        Text { x: 16; y: 12; color: Theme.text; font.pixelSize: 15; text: "Autosetup" }
        Text { anchors.right: parent.right; anchors.rightMargin: 16; y: 14; color: Theme.textDim; font.pixelSize: 10
               text: device.simulated ? "SIMULATED" : "" }

        Column {
            x: 16; y: 48; width: 388; spacing: 10
            visible: root.step === 0
            Text { color: Theme.text; font.pixelSize: 13; text: "Apply autosetup to:" }
            Repeater {
                model: [{k: "mic", t: "Microphone"}, {k: "inst", t: "Instrument"}, {k: "both", t: "Microphone + instrument"}]
                ToggleButton { width: 388; height: 30; text: modelData.t; active: root.target === modelData.k
                               onClicked: root.target = modelData.k }
            }
        }
        Column {
            x: 16; y: 48; width: 388; spacing: 10
            visible: root.step === 1
            Text { color: Theme.text; font.pixelSize: 13; text: "Does your microphone need +48 V?" }
            Text { width: 388; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 11
                   text: "Condenser microphones need phantom power. Passive dynamic and ribbon microphones do not, and ribbon microphones can be damaged." }
            Row { spacing: 8
                ToggleButton { width: 120; height: 30; text: "+48 V OFF"; active: !root.phantom; onClicked: root.phantom = false }
                ToggleButton { width: 120; height: 30; text: "+48 V ON"; active: root.phantom; activeColor: Theme.warn
                               onClicked: root.phantom = true } }
        }
        Column {
            x: 16; y: 48; width: 388; spacing: 10
            visible: root.step === 2
            Text { color: Theme.text; font.pixelSize: 13; text: "Choose a sound for your preamps" }
            Row { spacing: 8
                Repeater { model: ["Clean", "Warm", "Vivid"]
                    ToggleButton { width: 110; height: 30; text: modelData; active: root.preamp === index
                                   onClicked: root.preamp = index } } }
        }
        Column {
            x: 16; y: 48; width: 388; spacing: 10
            visible: root.step === 3
            Text { color: Theme.text; font.pixelSize: 13; text: "Would you like to reduce background noise?" }
            Text { width: 388; wrapMode: Text.WordWrap; color: root.dsp ? Theme.textDim : Theme.warn; font.pixelSize: 11
                   text: root.dsp ? "Denoise measures your noise floor (stay quiet for a few seconds)."
                                  : "Denoise is not available at 96 kHz. Choose a lower sample rate first." }
            Row { spacing: 8
                ToggleButton { width: 120; height: 30; text: "Denoise OFF"; active: !root.denoise; onClicked: root.denoise = false }
                ToggleButton { width: 120; height: 30; text: "Denoise ON"; active: root.denoise; unavailable: !root.dsp
                               onClicked: root.denoise = true } }
        }
        Column {
            x: 16; y: 48; width: 388; spacing: 10
            visible: root.step === 4
            Text { color: Theme.text; font.pixelSize: 13; text: "Would you like to use Clipguard?" }
            Text { width: 388; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 11
                   text: "Protects against distortion. Disable it for the highest possible dynamic range." }
            Row { spacing: 8
                ToggleButton { width: 120; height: 30; text: "Clipguard OFF"; active: !root.clipguard; onClicked: root.clipguard = false }
                ToggleButton { width: 120; height: 30; text: "Clipguard ON"; active: root.clipguard; onClicked: root.clipguard = true } }
        }
        Column {
            x: 16; y: 48; width: 388; spacing: 12
            visible: root.step === 5
            Text { color: Theme.text; font.pixelSize: 13
                   text: root.autoState === "running" ? "Perform for 10 seconds…" : "Let's test your sound!" }
            Text { width: 388; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 11
                   text: "Play or sing at the level you will record. The device measures and sets the gain." }
            Rectangle { width: 388; height: 10; radius: 5; color: Theme.well; border.color: Theme.line
                Rectangle { width: parent.width * Math.min(100, root.progress) / 100; height: parent.height; radius: 5
                            color: Theme.accent } }
            ToggleButton { width: 140; height: 30; text: "Start measure"; enabled: root.interactive && root.autoState !== "running"
                           onClicked: device.trigger(root.target === "mic" ? "autogain1"
                                                  : root.target === "inst" ? "autogain2" : "autogainBoth") }
        }
        Column {
            x: 16; y: 48; width: 388; spacing: 8
            visible: root.step === 6
            Text { color: Theme.text; font.pixelSize: 13; text: "Autosetup will apply these settings:" }
            Text { color: Theme.textDim; font.pixelSize: 11; lineHeight: 1.4
                   text: "Inputs: " + (root.target === "both" ? "microphone + instrument" : root.target) +
                         "\n+48 V: " + (root.phantom ? "on" : "off") +
                         "\nPreamp: " + ["Clean", "Warm", "Vivid"][root.preamp] +
                         "\nDenoise: " + (root.denoise && root.dsp ? "on" : "off") +
                         "\nClipguard: " + (root.clipguard ? "on" : "off") +
                         "\nMeasurement result code: " + (device.values["auto.resultCode"] === undefined ? "–" : device.values["auto.resultCode"]) +
                         " (meaning unverified)" }
        }

        Row {
            anchors.right: parent.right; anchors.rightMargin: 16; anchors.bottom: parent.bottom; anchors.bottomMargin: 14
            spacing: 8
            ToggleButton { width: 70; text: "Cancel"; onClicked: { root.step = 0; root.closeRequested(); } }
            ToggleButton { width: 70; text: "Back"; visible: root.step > 0; onClicked: root.step = Math.max(0, root.step - (root.step === 3 && root.target === "inst" ? 1 : 1)) }
            ToggleButton { width: 70; text: root.step === 6 ? "Apply" : "Next"; active: true
                           enabled: root.step !== 6 || root.interactive
                           onClicked: {
                               if (root.step === 6) { root.apply(); root.step = 0; root.closeRequested(); }
                               else if (root.step === 0 && root.target === "inst") root.step = 2;   // skip +48 V
                               else root.step += 1;
                           } }
        }
    }
}
