import QtQuick
import QtQuick.Window

Window {
    id: win
    width: 1008; height: 566; visible: true
    title: "OpenWitty — Linux control for LEWITT CONNECT 2 (unofficial)"
           + (device.diagnosticLegacyPolling ? " — DIAGNOSTIC 2/4 Hz · READ-ONLY" : "")
    color: Theme.footer
    minimumWidth: 720; minimumHeight: 440

    // One presentation state from the backend (see DeviceWorker::publish): disconnected |
    // simulated | real-readonly | real-writable | connecting | error | connection-lost
    readonly property string uiState: device.values["ui.state"] || "disconnected"
    readonly property string uiError: (device.values["ui.error"] || "").split("\n")[0]
    // transport name for display ("usb" -> "USB")
    readonly property string backendLabel: device.backend === "usb" ? "USB" : device.backend
    readonly property bool live: device.connection === "Connected"
    readonly property bool interactive: live && device.writable
    // Phase 10A: plain-language list of what the session may change on the real device (from can.<key>).
    function authorizedSummary() {
        var groups = [["speaker volume", "spk.volume"], ["speaker mute", "spk.mute"], ["headphone volume", "hp.volume"],
                      ["headphone mute", "hp.mute"], ["LED brightness", "set.ledBrightness"], ["input gain/mute", "in1.gain"],
                      ["input processing", "in1.highPass"], ["monitoring", "mon.mix"], ["LED colours/button", "set.customButton"],
                      ["48 V", "in1.phantom"], ["output modes", "set.highVolume"], ["autogain", "autogain1"]];
        var out = [];
        for (var i = 0; i < groups.length; ++i) if (device.values["can." + groups[i][1]] === true) out.push(groups[i][0]);
        var other = 0;
        for (var k in device.values)
            if (k.indexOf("can.") === 0 && device.values[k] === true) other++;
        if (other > out.length) out.push("other authorized controls");
        return out.length ? out.join(", ") : "nothing";
    }
    readonly property string selectedChannel: device.values["selected.channel"] || "unknown"
    readonly property bool panicMuted: win.live && device.values["mute.allFour"] === true
    readonly property string settingsFreshness: device.values["fresh.settings"] || "unavailable"
    readonly property string statusFreshness: device.values["fresh.status"] || "unavailable"
    readonly property bool dataStale: win.live && (settingsFreshness === "stale" || statusFreshness === "stale")
    readonly property string settingsWarning: settingsFreshness === "fresh" ? ""
        : "SETTINGS " + settingsFreshness.toUpperCase()
    readonly property string statusWarning: statusFreshness === "fresh" ? ""
        : "METERS " + statusFreshness.toUpperCase()
    readonly property string freshnessWarning: !win.live || (!settingsWarning && !statusWarning) ? ""
        : (settingsWarning && statusWarning ? settingsWarning + " · " + statusWarning
           : settingsWarning || statusWarning) + " — last known data; not confirmed current."
    property bool showSettings: false
    property bool showWizard: false
    property bool showOnboarding: false
    // Phase 10B: the tooltip preference is restored from gui.conf (when the application provides it)
    Component.onCompleted: if (typeof prefs !== "undefined") Hint.enabled = prefs.showTooltips
    // One frame-synchronized clock for eight presentation envelopes; never performs USB I/O.
    FrameAnimation {
        running: win.visible && win.active && win.live && device.meters.needsFrames
        onTriggered: device.meters.advanceFrame()
    }
    readonly property bool hasPending: Object.keys(device.values).some(function (k) { return k.indexOf("pending.") === 0; })
    Connections {
        target: device
        function onValuesChanged() {
            if (device.traceEnabled) Qt.callLater(device.traceQmlObserved)
        }
    }

    // Design space = the official window's proportions (720 x 373); scaled uniformly.
    Item {
        id: stage
        width: 720; height: 373
        scale: Math.min(win.width / width, (win.height - banner.height) / height)
        transformOrigin: Item.TopLeft
        x: (win.width - width * scale) / 2
        y: banner.height + ((win.height - banner.height) - height * scale) / 2

        Rectangle { anchors.fill: parent; color: Theme.darkPanel }
        Rectangle { x: 0; y: 50; width: 310; height: 295; color: Theme.inputPanel }   // inputs panel (incl. section bar)
        Rectangle { x: 0; y: 345; width: parent.width; height: 28; color: Theme.footer }
        Text { x: 10; y: 345; height: 28; verticalAlignment: Text.AlignVCenter; color: "#f0f0f0"
               font.pixelSize: 13; text: Hint.text }   // control hints live in the status bar, as in the original
        Text {  // Phase 10B.1: a real-device write was sent but not confirmed; nothing more is sent in this session
            objectName: "writeLockedFooter"; visible: device.values["write.locked"] === true && !win.hasPending
            anchors.right: parent.right; anchors.rightMargin: 8; y: 345; height: 28; verticalAlignment: Text.AlignVCenter
            color: Theme.warn; font.pixelSize: 11
            text: "Last change not confirmed — device writes stopped (see Settings → Device control)" }
        Row {   // unsent edits: never silently dropped; user decides
            visible: win.hasPending; anchors.right: parent.right; anchors.rightMargin: 8; y: 349; spacing: 6
            Text { color: Theme.warn; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter
                   text: "Change not sent — " + device.lastError }
            ToggleButton { width: 52; height: 20; text: "Retry"; primary: true; onClicked: device.retryPending() }
            ToggleButton { width: 58; height: 20; text: "Discard"; onClicked: device.discardPending() }
        }

        // header
        Rectangle { x: 0; y: 0; width: parent.width; height: 50; color: Theme.header
            Text { x: 14; anchors.verticalCenter: parent.verticalCenter; color: Theme.text; font.pixelSize: 15
                   font.letterSpacing: 0.5; text: "CONTROL CENTER" }
            Text { x: 152; anchors.verticalCenter: parent.verticalCenter; color: Theme.accent; font.pixelSize: 15
                   text: "| CONNECT 2" }
            GlyphIcon { x: 14; y: 13; width: 24; height: 24; kind: "headphones"; color: Theme.accent
                        visible: false }
            DeviceStatusIndicator { objectName: "headerConnectionIndicator"; x: 262; anchors.verticalCenter: parent.verticalCenter
                // the presented state: a hand-over from the background service reads "Connecting", not "Error"
                state: win.uiState === "connecting" ? "Connecting" : device.connection
                detail: win.uiState === "connecting" ? win.backendLabel
                      : win.uiState === "simulated" ? "simulated"
                      : win.uiState === "real-readonly" ? "read-only"
                      : win.uiState === "real-writable" ? "real device"
                      : win.uiState === "connection-lost" ? "connection lost"
                      : win.backendLabel }
            Text { objectName: "sampleRateLabel"; x: 461; anchors.verticalCenter: parent.verticalCenter; color: Theme.textDim; font.pixelSize: 11
                   text: "Sample rate:" }
            Rectangle {   // sample-rate dropdown (simulation only; real rate comes from the audio stack)
                id: rateBox
                x: 549; y: 14; width: 120; height: 26; radius: 3; color: "#515151"
                border.color: "#5a5a5a"
                opacity: device.simulated ? 1 : 0.55
                Text { x: 8; anchors.verticalCenter: parent.verticalCenter; color: "#f0f0f0"; font.pixelSize: 11
                       text: device.values["sampleRate"] === undefined ? "Unknown"
                             : (device.values["sampleRate"] / 1000).toString() + " kHz" }
                Text { anchors.right: parent.right; anchors.rightMargin: 7; anchors.verticalCenter: parent.verticalCenter
                       color: "#777"; font.pixelSize: 9; text: "▴\n▾"; lineHeight: 0.75 }
                MouseArea { anchors.fill: parent; enabled: device.simulated; onClicked: rateMenu.visible = !rateMenu.visible }
                ToolTip_ { visible: rateMa.containsMouse; text: device.simulated ? "Simulated sample rate"
                                                              : "Set by the audio system (not controlled here)" }
                MouseArea { id: rateMa; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
            }
            ToggleButton { x: 686; y: 16; width: 21; height: 22; icon: "settings"
                           tip: "Open the settings panel"; onClicked: win.showSettings = true }
        }
        Column {
            id: rateMenu
            visible: false; x: 549; y: 41; z: 30
            Repeater {
                model: [44100, 48000, 96000]
                Rectangle { width: 120; height: 22; color: ma.containsMouse ? "#3a3a3a" : "#222"; border.color: "#333"
                    Text { x: 8; anchors.verticalCenter: parent.verticalCenter; color: Theme.text; font.pixelSize: 11
                           text: (modelData / 1000) + " kHz" }
                    MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true
                                onClicked: { device.setSampleRate(modelData); rateMenu.visible = false; } }
                }
            }
        }

        // section bars
        Rectangle { x: 310; y: 50; width: parent.width - 310; height: 28; color: Theme.sectionBar }
        Rectangle { x: 0; y: 77; width: parent.width; height: 1; color: "#141314" }
        Text { x: 0; width: 310; y: 56; horizontalAlignment: Text.AlignHCenter; color: "#f2f2f2"; font.pixelSize: 11
               font.bold: true; text: "Inputs" }
        Text { x: 310; width: 255; y: 56; horizontalAlignment: Text.AlignHCenter; color: "#f2f2f2"; font.pixelSize: 11
               font.bold: true; text: "Outputs" }
        Text { x: 565; width: 155; y: 56; horizontalAlignment: Text.AlignHCenter; color: "#f2f2f2"; font.pixelSize: 11
               font.bold: true; text: "Monitoring" }
        ToggleButton { objectName: "autosetupButton"; x: 17; y: 55; width: 69; height: 19; text: "Autosetup"; enabled: win.interactive && device.values["can.autosetup"] === true
                       tip: "Automatically find the perfect settings for your inputs"; onClicked: win.showWizard = true }
        // vertical dividers
        Rectangle { x: 155; y: 78; width: 1; height: 267; color: "#1b1b1b" }
        Rectangle { x: 310; y: 50; width: 1; height: 295; color: "#000" }
        Rectangle { x: 395; y: 78; width: 1; height: 267; color: "#1a1a1a" }
        Rectangle { x: 480; y: 78; width: 1; height: 267; color: "#1a1a1a" }
        Rectangle { x: 565; y: 50; width: 1; height: 295; color: "#000" }

        InputStrip { x: 3; y: 78; index: 1; stageScale: stage.scale; interactive: win.interactive; physicalSelected: win.selectedChannel === "microphone" }
        InputStrip { x: 157; y: 78; index: 2; stageScale: stage.scale; interactive: win.interactive; physicalSelected: win.selectedChannel === "instrument" }
        OutputStrip { x: 310; y: 78; group: "loop"; stageScale: stage.scale; interactive: win.interactive }
        OutputStrip { x: 395; y: 78; group: "spk"; stageScale: stage.scale; interactive: win.interactive; physicalSelected: win.selectedChannel === "speaker" }
        OutputStrip { x: 480; y: 78; group: "hp"; stageScale: stage.scale; interactive: win.interactive; physicalSelected: win.selectedChannel === "headphone" }
        MonitorSection { x: 568; y: 78; interactive: win.interactive }

        // overlays
        Rectangle { visible: !win.live; x: 0; y: 50; width: parent.width; height: 323; color: "#1f1f1f"; z: 5
            Text { anchors.centerIn: parent; color: "#fff"; font.pixelSize: 20; font.bold: true
                   text: device.values["restarting"] ? "Device restarting… (simulated)"
                         : win.uiState === "connecting" ? "Connecting…" : "No LEWITT device connected!" }
            Text { anchors.horizontalCenter: parent.horizontalCenter; y: parent.height / 2 + 26; color: Theme.textDim
                   font.pixelSize: 11; text: win.uiState === "connecting" ? (device.values["ui.detail"] || "")
                                         : device.lastError.length ? device.lastError
                                         : "Use the buttons above to start the simulated device." }
        }
        SettingsView { objectName: "settingsView"; visible: win.showSettings; x: 0; y: 0; width: parent.width; height: parent.height; z: 10
                       interactive: win.interactive; onCloseRequested: win.showSettings = false
                       onOnboardingRequested: { win.showSettings = false; win.showOnboarding = true; } }
        OnboardingOverlay { objectName: "onboardingOverlay"; visible: win.showOnboarding; x: 0; y: 0; width: parent.width; height: parent.height; z: 12
                            onFinished: { win.showOnboarding = false;
                                          if (typeof prefs !== "undefined") prefs.setOnboardingDone(true); } }
        AutosetupWizard { visible: win.showWizard; x: 0; y: 0; width: parent.width; height: parent.height; z: 11
                          interactive: win.interactive && device.values["can.autosetup"] === true
                          onCloseRequested: win.showWizard = false }
    }

    // simulation banner + development controls (outside the design space)
    Rectangle {
        id: banner
        width: parent.width; height: 28
        color: win.dataStale ? "#3a231f"
             : win.freshnessWarning.length ? "#3a301d"
             : win.uiState === "simulated" ? "#2e2813"
             : win.uiState === "real-readonly" || win.uiState === "real-writable" ? "#162633"
             : win.uiState === "error" || win.uiState === "connection-lost" ? "#2b1d1d" : "#232323"
        Text { objectName: "connectionBannerText"; anchors.verticalCenter: parent.verticalCenter; x: 10; font.pixelSize: 11
               width: Math.max(0, (panicMuteIndicator.visible ? panicMuteIndicator.x : controls.x) - x - 10)
               elide: Text.ElideRight
               color: win.dataStale ? "#ffb4a8"
                    : win.freshnessWarning.length ? Theme.warn
                    : win.uiState === "simulated" ? Theme.sim
                    : win.uiState === "real-readonly" || win.uiState === "real-writable" ? "#9ec7ff"
                    : win.uiState === "error" || win.uiState === "connection-lost" ? Theme.bad : Theme.textDim
               text: win.freshnessWarning.length
                     ? (win.uiState === "simulated" ? "SIMULATED DEVICE" : "CONTROL LINK, READ-ONLY")
                       + " — " + win.freshnessWarning
                     : win.uiState === "simulated"
                     ? "SIMULATED DEVICE — no hardware is controlled; values shown are not device data."
                     : win.uiState === "real-readonly"
                     ? "CONTROL LINK, READ-ONLY — " + (device.values["ui.device"] || "CONNECT 2")
                       + ". Audio and front-panel readiness not verified."
                     : win.uiState === "real-writable"
                     ? "REAL DEVICE — can change: " + win.authorizedSummary() + ". All other controls are display-only."
                     : win.uiState === "connecting"
                     ? (device.values["ui.detail"] === "Taking over from the OpenWitty background service"
                        ? "Connecting — taking over the CONNECT 2 from the OpenWitty background service…"
                        : "Connecting to the device…")
                     : win.uiState === "connection-lost"
                     ? "Connection to the device was lost — retrying when it reappears. " + win.uiError
                     : win.uiState === "error" ? "Cannot communicate with the device. " + win.uiError
                     : "No device connected. Use “USB (read-only)” to look for a CONNECT 2." }
        Text { id: panicMuteIndicator; objectName: "panicMuteIndicator"; visible: win.panicMuted; anchors.verticalCenter: parent.verticalCenter; anchors.right: controls.left
               anchors.rightMargin: 10; color: "#ff9a9a"; font.pixelSize: 10; font.bold: true
               text: "ALL MAIN CHANNELS MUTED" }
        Row { id: controls; anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter; spacing: 6
            ToggleButton { width: 90; height: 20; text: "Mock device"; onClicked: device.connectMock() }
            ToggleButton { width: 100; height: 20; text: "USB (read-only)"; onClicked: device.connectUsb() }
            ToggleButton { width: 52; height: 20; text: "Unplug"; visible: device.simulated; onClicked: device.simulateUnplug() }
            ToggleButton { width: 52; height: 20; text: "Replug"; visible: device.simulated; onClicked: device.simulateReplug() }
        }
    }
}
