import QtQuick

// Autosetup: chooser with three columns (Microphone / Instrument / Mic + instrument,
// bullets + estimated duration), then the guided steps (+48 V, preamp character,
// Denoise, Clipguard, timed measurement, summary). Chooser layout and durations
// follow public screenshots; step content follows the app's strings. The
// individual choices are applied through ordinary settings writes; the
// measurement uses the auto-gain trigger. Not protocol-verified.
Rectangle {
    id: root
    property bool interactive: false
    signal closeRequested()
    color: "#262626"
    property int step: -1                // -1 = chooser; 1..5 guided steps, 7 ambient noise test, 6 summary
    property string target: "both"       // mic | inst | both
    property bool phantom: false
    property int preamp: 0
    property bool denoise: false
    property bool clipguard: true
    readonly property bool dsp: device.values["dsp.available"] !== false
    readonly property string autoState: device.values["auto.state"] || "idle"
    readonly property int progress: device.values["auto.progress"] || 0
    readonly property int outcome: device.values["auto.resultCode"] === undefined ? 0 : device.values["auto.resultCode"]
    // Phase 11B session 4: on hardware a successful mic autogain reported result code 1, which the decoded reading calls
    // "failed". Until the code is understood, only the simulated device's outcome is shown as a failure.
    readonly property bool failed: device.simulated && autoState === "finished" && outcome === 1
    // 2026-11-01: 48 V is switched on before the measurement (a condenser mic gives no signal without it); the
    // measurement waits a few seconds after switching so the capsule can charge.
    property bool phantomSettling: false
    Timer { id: phantomTimer; interval: 5000; onTriggered: root.phantomSettling = false }
    function phantomBeforeMeasuring() {
        if (phantom && target !== "inst" && device.values["in1.phantom"] !== true) {
            device.setValue("in1.phantom", true);
            phantomSettling = true;
            phantomTimer.restart();
        }
    }
    // Ambient noise test (official "Now test your ambient noise"): only when Denoise is chosen; mic, then instrument.
    readonly property bool noiseTest: denoise && dsp
    property var noiseQueue: []
    property string noiseRunning: ""
    function startNoiseTest() {
        noiseQueue = target === "mic" ? ["denoise1"] : target === "inst" ? ["denoise2"] : ["denoise1", "denoise2"];
        nextNoise();
    }
    function nextNoise() {
        if (noiseQueue.length === 0) { noiseRunning = ""; step = 6; return; }
        noiseRunning = noiseQueue[0];
        noiseQueue = noiseQueue.slice(1);
        device.trigger(noiseRunning);
    }
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; hoverEnabled: true
                onWheel: (w) => { w.accepted = true; } }   // modal: no click or wheel reaches the main view

    function apply() {
        var ins = target === "mic" ? [1] : target === "inst" ? [2] : [1, 2];
        for (var i = 0; i < ins.length; ++i) {
            var p = "in" + ins[i] + ".";
            device.setValue(p + "preamp", preamp);
            device.setValue(p + "clipguard", clipguard);
            if (dsp) device.setValue(p + "denoise", denoise);
        }
        if (phantom && ins.indexOf(1) >= 0 && device.values["in1.phantom"] !== true) device.setValue("in1.phantom", true);
    }
    // Advance when the measurement finished (on the real device a successful autogain reports result code 1).
    onAutoStateChanged: {
        if (autoState !== "finished" && autoState !== "timeout") return;
        if (step === 5 && !failed) step = noiseTest ? 7 : 6;
        else if (step === 7 && noiseRunning !== "") nextNoise();
    }

    // title bar of the "wizard window"
    Rectangle { x: 0; y: 0; width: parent.width; height: 22; color: "#343434"
        Text { anchors.centerIn: parent; text: "Autosetup wizard"; color: "#cfcfcf"; font.pixelSize: 11; font.bold: true }
        Text { x: parent.width - 80; anchors.verticalCenter: parent.verticalCenter; color: Theme.sim; font.pixelSize: 9
               text: device.simulated ? "SIMULATED" : "" }
        MouseArea { x: 0; y: 0; width: 22; height: 22; onClicked: { root.step = -1; root.closeRequested(); } }
        Rectangle { x: 8; y: 6; width: 10; height: 10; radius: 5; color: "#ff5f57" }
    }

    // chooser
    Item {
        visible: root.step < 0; x: 0; y: 22; width: parent.width; height: parent.height - 22
        Rectangle { x: 0; y: 0; width: parent.width; height: 62; color: "#1f1f1f"
            Text { anchors.centerIn: parent; text: "Apply autosetup to:"; color: "#fff"; font.pixelSize: 22 } }
        Repeater {
            model: [
                { k: "mic", t: "Microphone", icon: "mic", b: "Guided walkthrough of all settings\n45 sec", tag: "+48V" },
                { k: "inst", t: "Instrument", icon: "guitar", b: "Guided walkthrough of all settings\n45 sec", tag: "" },
                { k: "both", t: "Mic + instrument", icon: "both", b: "Quick setup of mic and instrument gain\n10 sec", tag: "" }]
            Rectangle {
                id: col
                property var o: modelData
                x: index * (parent.width / 3); y: 62; width: parent.width / 3; height: parent.height - 62
                color: index === 1 ? "#222" : "#2a2a2a"
                Item {
                    x: 0; y: 28; width: parent.width; height: 90
                    GlyphIcon { visible: col.o.icon !== "both"; x: parent.width / 2 - 36; y: 0; width: 72; height: 72
                                kind: col.o.icon; color: "#f4f4f4" }
                    Row { visible: col.o.icon === "both"; x: parent.width / 2 - 70; spacing: 14
                        GlyphIcon { width: 60; height: 60; kind: "mic"; color: "#f4f4f4" }
                        Text { text: "+"; color: "#f4f4f4"; font.pixelSize: 26; anchors.verticalCenter: parent.verticalCenter }
                        GlyphIcon { width: 60; height: 60; kind: "guitar"; color: "#f4f4f4" } }
                    Text { visible: col.o.tag !== ""; x: parent.width / 2 + 16; y: 56; color: "#f4f4f4"
                           font.pixelSize: 11; font.bold: true; text: col.o.tag }
                }
                Text { x: 22; y: 138; width: parent.width - 44; color: "#eee"; font.pixelSize: 11; lineHeight: 1.35
                       wrapMode: Text.WordWrap; text: "•  " + col.o.b.replace("\n", "\n•  ") }
                ToggleButton { x: parent.width / 2 - 55; y: parent.height - 52; width: 110; height: 26
                               text: col.o.t; primary: true; enabled: root.interactive
                               onClicked: { root.target = col.o.k; root.step = col.o.k === "inst" ? 2 : (col.o.k === "mic" ? 1 : 2); } }
            }
        }
    }

    // guided steps
    Item {
        visible: root.step >= 0; x: 0; y: 22; width: parent.width; height: parent.height - 22
        Rectangle { x: 0; y: 0; width: parent.width; height: 48; color: "#1f1f1f"
            Text { anchors.centerIn: parent; color: "#fff"; font.pixelSize: 18
                   text: ["", "Does your microphone need +48 V?", "Choose a sound for your preamps",
                          "Would you like to reduce background noise?", "Would you like to use Clipguard?",
                          "Let's test your sound!", "Autosetup will apply these settings",
                          "Now test your ambient noise"][Math.max(0, root.step)] } }
        Column {
            x: 40; y: 66; width: parent.width - 80; spacing: 12
            visible: root.step === 1
            Text { width: parent.width; wrapMode: Text.WordWrap; color: "#cfcfcf"; font.pixelSize: 12
                   text: "Condenser microphones need phantom power. Passive dynamic and ribbon microphones do not, and ribbon microphones can be damaged." }
            Row { spacing: 10
                ToggleButton { width: 130; height: 30; text: "+48 V OFF"; active: !root.phantom; onClicked: root.phantom = false }
                ToggleButton { width: 130; height: 30; text: "+48 V ON"; active: root.phantom; activeColor: Theme.warn
                               onClicked: root.phantom = true } }
        }
        Column {
            x: 40; y: 66; width: parent.width - 80; spacing: 12; visible: root.step === 2
            Row { spacing: 10
                Repeater { model: [{n: "Clean", d: "Unparalleled clarity"}, {n: "Warm", d: "Smooth high end"}, {n: "Vivid", d: "Open and lively sound"}]
                    Column { spacing: 6
                        ToggleButton { width: 130; height: 30; text: modelData.n; active: root.preamp === index
                                       onClicked: root.preamp = index }
                        Text { width: 130; horizontalAlignment: Text.AlignHCenter; color: "#a8a8a8"; font.pixelSize: 10; text: modelData.d } } } }
        }
        Column {
            x: 40; y: 66; width: parent.width - 80; spacing: 12; visible: root.step === 3
            Text { width: parent.width; wrapMode: Text.WordWrap; color: root.dsp ? "#cfcfcf" : Theme.warn; font.pixelSize: 12
                   text: root.dsp ? "Denoise measures your noise floor (stay quiet for a few seconds)."
                                  : "Denoise is not available at 96 kHz. Choose a lower sample rate first." }
            Row { spacing: 10
                ToggleButton { width: 130; height: 30; text: "Denoise OFF"; active: !root.denoise; onClicked: root.denoise = false }
                ToggleButton { width: 130; height: 30; text: "Denoise ON"; active: root.denoise; unavailable: !root.dsp
                               onClicked: root.denoise = true } }
        }
        Column {
            x: 40; y: 66; width: parent.width - 80; spacing: 12; visible: root.step === 4
            Text { width: parent.width; wrapMode: Text.WordWrap; color: "#cfcfcf"; font.pixelSize: 12
                   text: "Protection from distortion. Disable it for the highest possible dynamic range." }
            Row { spacing: 10
                ToggleButton { width: 130; height: 30; text: "Clipguard OFF"; active: !root.clipguard; onClicked: root.clipguard = false }
                ToggleButton { width: 130; height: 30; text: "Clipguard ON"; active: root.clipguard; onClicked: root.clipguard = true } }
        }
        Column {
            x: 40; y: 66; width: parent.width - 80; spacing: 12; visible: root.step === 5
            Text { width: parent.width; wrapMode: Text.WordWrap; font.pixelSize: 12
                   color: root.failed ? Theme.danger : "#cfcfcf"
                   text: root.failed ? "No signal detected! Please repeat measurement."
                         : root.autoState === "running" ? "Perform for 10 seconds…"
                         : "Play or sing at the level you will record. The device measures and sets the gain." }
            Rectangle { width: parent.width; height: 10; radius: 5; color: Theme.well
                Rectangle { width: parent.width * Math.min(100, root.progress) / 100; height: parent.height; radius: 5
                            color: root.failed ? Theme.danger : Theme.meterGreen } }
            Text { visible: root.phantomSettling; color: Theme.warn; font.pixelSize: 11
                   text: "+48 V was just switched on: wait a moment for the microphone…" }
            ToggleButton { width: 140; height: 30; text: root.failed ? "Repeat" : "Start measure"; primary: true
                           enabled: root.interactive && root.autoState !== "running" && !root.phantomSettling
                           onClicked: device.trigger(root.target === "mic" ? "autogain1"
                                                  : root.target === "inst" ? "autogain2" : "autogainBoth") }
        }
        Column {
            x: 40; y: 66; width: parent.width - 80; spacing: 12; visible: root.step === 7
            Text { width: parent.width; wrapMode: Text.WordWrap; color: "#cfcfcf"; font.pixelSize: 12
                   text: root.noiseRunning !== "" ? "Stay quiet… measuring the background noise"
                         + (root.target === "both" ? (root.noiseRunning === "denoise1" ? " (microphone)" : " (instrument)") : "")
                         : "Stay quiet for about 10 seconds while the device measures the background noise. "
                           + "It then switches Denoise on by itself." }
            Rectangle { width: parent.width; height: 10; radius: 5; color: Theme.well
                Rectangle { width: parent.width * Math.min(100, root.noiseRunning !== "" ? root.progress : 0) / 100
                            height: parent.height; radius: 5; color: Theme.meterGreen } }
            ToggleButton { objectName: "autosetupNoiseStart"; width: 140; height: 30; text: "Start measure"; primary: true
                           enabled: root.interactive && root.noiseRunning === "" && root.autoState !== "running"
                           onClicked: root.startNoiseTest() }
        }
        Column {
            x: 40; y: 66; width: parent.width - 80; spacing: 8; visible: root.step === 6
            Text { color: "#cfcfcf"; font.pixelSize: 12; lineHeight: 1.5
                   text: "Inputs: " + (root.target === "both" ? "microphone + instrument" : root.target) +
                         "\n+48 V: " + (root.phantom && root.target !== "inst" ? "on" : "off") +
                         "\nPreamp: " + ["Clean", "Warm", "Vivid"][root.preamp] +
                         "\nDenoise: " + (root.denoise && root.dsp ? "on" : "off") +
                         "\nClipguard: " + (root.clipguard ? "on" : "off") }
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: 16; anchors.bottom: parent.bottom; anchors.bottomMargin: 14
            spacing: 8
            ToggleButton { width: 70; height: 26; text: "Cancel"; onClicked: { root.step = -1; root.closeRequested(); } }
            ToggleButton { width: 70; height: 26; text: "Back"; active: true
                           enabled: root.noiseRunning === "" && root.autoState !== "running"
                           onClicked: root.step = root.step === 7 ? 5
                                      : (root.step <= 1 || (root.step === 2 && root.target !== "mic" && root.target !== "both")) ? -1 : root.step - 1 }
            ToggleButton { width: 70; height: 26; text: root.step === 6 ? "Apply" : "Next"; primary: true
                           enabled: (root.step !== 6 || root.interactive) && root.noiseRunning === "" && root.autoState !== "running"
                           onClicked: {
                               if (root.step === 6) { root.apply(); root.step = -1; root.closeRequested(); }
                               else if (root.step === 1) { root.phantomBeforeMeasuring(); root.step = 2; }
                               else if (root.step === 5) root.step = root.noiseTest ? 7 : 6;   // skip the measurement
                               else if (root.step === 7) root.step = 6;                       // skip the noise test
                               else root.step += 1;
                           } }
        }
    }
}
