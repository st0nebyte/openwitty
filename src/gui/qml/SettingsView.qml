import QtQuick

// Settings page (guide: Audio / LEDs / Custom button on the left, versions and
// maintenance on the right). Items without verified protocol support are shown
// disabled with the reason.
Rectangle {
    id: root
    property bool interactive: false
    signal closeRequested()
    signal onboardingRequested()
    color: "#1e1e1e"
    // Phase 11B: the page is opaque to input. Clicks and wheel turns on its empty areas must not reach the
    // controls of the main view underneath (session 1: a wheel over the page lowered the instrument gain).
    MouseArea { objectName: "settingsInputBlocker"; anchors.fill: parent; acceptedButtons: Qt.AllButtons
                hoverEnabled: true; onWheel: (w) => { w.accepted = true; } }
    function v(k, d) { var x = device.values[k]; return x === undefined ? d : x; }
    // Phase 10A: a control is enabled only if the backend says its exact bytes/trigger are authorized.
    function can(k) { return root.interactive && device.values["can." + k] === true; }
    property string open: "audio"
    // Phase 10B.1: outcome of the last deliberate LED brightness change on the real device
    // ("" | pending | verified | unchanged | not-sent | unconfirmed | refused).
    property string ledWrite: ""
    property var ledWriteSeq: 0
    readonly property var writeSeqNow: device.values["write.seq"]
    onWriteSeqNowChanged: {
        if (ledWrite === "pending" && writeSeqNow !== ledWriteSeq && device.values["write.key"] === "set.ledBrightness")
            ledWrite = device.values["write.outcome"] || "";
    }
    function commitLed(nv) {
        ledWriteSeq = device.values["write.seq"];
        ledWrite = "pending";
        device.setValue("set.ledBrightness", nv);
    }
    function ledWriteText() {
        var msg = device.values["write.message"] || "";
        switch (ledWrite) {
        case "pending": return "Sending to the device…";
        case "verified": return "Verified: the device reports " + root.v("set.ledBrightness", 0) + " %.";
        case "unchanged": return "The device already has this value; nothing was sent.";
        case "not-sent": return "Not sent: " + msg;
        case "unconfirmed": return "Not confirmed: " + msg + ". Nothing more is sent; check the device.";
        case "refused": return "Refused: " + msg;
        }
        return "";
    }
    // Phase 11A: grouped, from the backend's per-control capabilities (can.<key>).
    readonly property var capabilityGroups: [
        ["speaker volume/mute", ["spk.volume", "spk.mute"]], ["headphone volume/mute", ["hp.volume", "hp.mute"]],
        ["LED brightness", ["set.ledBrightness"]], ["input gain/mute", ["in1.gain", "in2.gain", "in1.mute", "in2.mute"]],
        ["input processing", ["in1.highPass", "in1.clipguard", "in1.preamp", "in1.denoise", "in1.compress"]],
        ["monitoring", ["mon.mix", "mon.mic", "out.dim", "out.mono", "loop.mute"]],
        ["LED colours and custom button", ["set.led.icons", "set.customButton"]], ["48 V", ["in1.phantom"]],
        ["output modes", ["set.highVolume", "set.highImpedance"]], ["routing", ["set.mixToSpeakers", "set.mobileStreaming"]], ["autogain", ["autogain1"]]]
    function authorizedSummary() {
        var out = [];
        for (var i = 0; i < capabilityGroups.length; ++i) {
            var keys = capabilityGroups[i][1], any = false;
            for (var j = 0; j < keys.length; ++j) if (device.values["can." + keys[j]] === true) any = true;
            if (any) out.push(capabilityGroups[i][0]);
        }
        return out.length ? out.join(", ") : "nothing (display only)";
    }
    // Phase 11A: the further permissions (name, switch label, tooltip, confirmation text).
    readonly property var extraPermissions: [
        {n: "inputs", label: "Input gain and mute",
         confirm: "OpenWitty will be able to change the microphone and instrument gain and mute and the external gain lock. "
                + "Gain can rise by at most 6 dB per step. Unmuting or raising gain can cause feedback through monitoring: "
                + "keep speakers down and headphones off while you test."},
        {n: "processing", label: "Input processing",
         confirm: "OpenWitty will be able to switch low-cut, Clipguard, preamp style, Denoise and Comp of both inputs. "
                + "Denoise is switched without the official noise measurement. Each change is read back."},
        {n: "monitoring", label: "Monitoring",
         confirm: "OpenWitty will be able to change direct monitoring (source and input/playback mix), Dim, Mono and the "
                + "loopback mute. Monitoring changes what you hear and can create feedback; never monitor the loopback."},
        {n: "appearance", label: "LED colours and custom button",
         confirm: "OpenWitty will be able to change the LED colours and the custom button function. These are stored on "
                + "the device. Each change is read back."},
        {n: "phantom", label: "48 V phantom power",
         confirm: "OpenWitty will be able to switch 48 V phantom power on the microphone input; each switch-on asks again. "
                + "Phantom power can damage some ribbon microphones and causes a pop: mute outputs first."},
        {n: "outputModes", label: "Output modes (louder)",
         confirm: "OpenWitty will be able to switch high-volume speaker mode and high-impedance headphone mode; each "
                + "switch-on asks again. Both can make the output much louder."},
        {n: "followOutput", label: "Follow active output (system volume)",
         confirm: "OpenWitty will switch which CONNECT 2 output the system volume keys control (\"Map native volume "
                + "controls to\") to the output you can hear, whenever the speaker/headphone mutes change - also from "
                + "the background service, and moves the touch-wheel focus along (can be switched off). Volume keys then "
                + "act on that output directly; nothing else is written."},
        {n: "routing", label: "Routing (mix to speakers, mobile streaming)",
         confirm: "OpenWitty will be able to send the monitoring mix to the speakers and switch mobile streaming mode "
                + "(inputs 1/2 carry the mic + instrument mix); each switch-on asks again."},
        {n: "measurements", label: "Autogain and Denoise measurements",
         confirm: "OpenWitty will be able to start the CONNECT 2's autogain and Denoise measurements (each asks again). The "
                + "device then sets the input gain or Denoise by itself. Keep speakers down and headphones off during a measurement."}]

    Text { x: 16; y: 12; text: "Settings"; color: Theme.text; font.pixelSize: 15 }
    ToggleButton { anchors.right: parent.right; anchors.rightMargin: 16; y: 10; width: 60; text: "Close"
                   onClicked: root.closeRequested() }
    Rectangle { x: 0; y: 44; width: parent.width; height: 1; color: Theme.line }

    // Accordion: headers up to the open one at the top, its section directly below it, the remaining headers at the
    // bottom of the column.
    readonly property var sections: ["audio", "leds", "button", "device", "desktop"]
    readonly property int openIndex: Math.max(0, sections.indexOf(open))
    readonly property int headerStep: 32
    function headerY(i) {
        return i <= openIndex ? 60 + i * headerStep : root.height - 8 - (sections.length - i) * headerStep + 6;
    }
    readonly property real bodyY: 60 + (openIndex + 1) * headerStep + 4
    readonly property real bodyH: headerY(openIndex + 1 < sections.length ? openIndex + 1 : sections.length) - bodyY - 8
    Item {
        x: 16; y: 0; width: 330; height: root.height
        Repeater {
            model: [{k: "audio", t: "Audio"}, {k: "leds", t: "LEDs"}, {k: "button", t: "Custom button"},
                    {k: "device", t: "Device control"}, {k: "desktop", t: "Desktop integration"}]
            Rectangle {
                objectName: "sectionHeader_" + modelData.k
                y: root.headerY(index)
                width: 330; height: 26; color: Theme.panel; radius: 2
                Text { x: 8; anchors.verticalCenter: parent.verticalCenter; color: Theme.text; font.pixelSize: 12; text: modelData.t }
                Text { anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
                       color: Theme.textDim; text: root.open === modelData.k ? "▾" : "◂" }
                MouseArea { anchors.fill: parent; onClicked: root.open = modelData.k }
            }
        }
    }

    // Section bodies
    Flickable {
        objectName: "audioPage"
        visible: root.open === "audio"
        x: 16; y: root.bodyY; width: 340; height: root.bodyH; clip: true
        contentHeight: audioCol.height
    Column {
        id: audioCol
        width: 330; spacing: 6
        ToggleSwitch { objectName: "gainLockSwitch"; width: 330; label: "Gain lock (block external gain changes)"; enabled: root.can("set.gainLock")
                       checked: root.v("set.gainLock", false)
                       onToggled: (b) => device.setValue("set.gainLock", b) }
        ToggleSwitch { objectName: "mixToSpeakersSwitch"; width: 330; label: "Input/playback mix applies to speakers"
                       enabled: root.can("set.mixToSpeakers")
                       checked: root.v("set.mixToSpeakers", false)
                       tip: "The speakers play the monitoring mix (inputs + computer) instead of the computer only"
                       onToggled: (b) => confirmLoud.ask("set.mixToSpeakers", b) }
        ToggleSwitch { width: 330; label: "High impedance headphone mode"; enabled: root.can("set.highImpedance")
                       checked: root.v("set.highImpedance", false)
                       onToggled: (b) => confirmLoud.ask("set.highImpedance", b) }
        ToggleSwitch { width: 330; label: "High volume speaker mode"; enabled: root.can("set.highVolume")
                       checked: root.v("set.highVolume", false)
                       onToggled: (b) => confirmLoud.ask("set.highVolume", b) }
        ToggleSwitch { objectName: "mobileStreamingSwitch"; width: 330; label: "Mobile streaming mode"
                       enabled: root.can("set.mobileStreaming")
                       checked: root.v("set.mobileStreaming", false)
                       tip: "Microphone and instrument mix to software input 1/2 (official description)"
                       onToggled: (b) => confirmLoud.ask("set.mobileStreaming", b) }
        // Phase 12C: follow active output (owner spec): OpenWitty keeps byte 59 on the audible output
        ToggleSwitch { objectName: "followOutputSwitch"; width: 330; label: "Follow active output"
                       checked: integration.permissions["followOutput"] === true
                       tip: "The system volume keys control the output you can hear (speakers or headphones)"
                       onToggled: (b) => { if (b) confirmWrite.ask("followOutput"); else integration.setPermission("followOutput", false, false); } }
        Text { width: 330; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 10
               visible: integration.permissions["followOutput"] === true
               text: "Speakers on only: speakers. Headphones on only: headphones. Needs desktop volume sync (Desktop integration)."
                     + (integration.desktopSync ? "" : " Desktop volume sync is off: nothing follows right now.") }
        ToggleSwitch { objectName: "followFocusSwitch"; width: 330; label: "Also move the touch-wheel focus"
                       visible: integration.permissions["followOutput"] === true
                       checked: integration.followMoveFocus
                       tip: "The touch wheel on the CONNECT 2 then controls the same output as the volume keys"
                       onToggled: (b) => integration.setFollowMoveFocus(b) }
        Text { width: 330; color: Theme.text; font.pixelSize: 11; visible: integration.permissions["followOutput"] === true
               text: "When both outputs are on, the volume keys control:" }
        Row { spacing: 6; visible: integration.permissions["followOutput"] === true
            Repeater {
                model: [{v: "speakers", t: "Speakers"}, {v: "headphones", t: "Headphones"}, {v: "keep", t: "Keep last"}]
                ToggleButton { objectName: "followBothOn_" + modelData.v; width: 90; height: 22; text: modelData.t
                               active: integration.followBothOn === modelData.v
                               onClicked: integration.setFollowBothOn(modelData.v) } } }
        Text { width: 330; color: Theme.text; font.pixelSize: 11; visible: integration.permissions["followOutput"] === true
               text: "When both outputs are muted:" }
        Row { spacing: 6; visible: integration.permissions["followOutput"] === true
            Repeater {
                model: [{v: "keep", t: "Keep last"}, {v: "speakers", t: "Speakers"}, {v: "headphones", t: "Headphones"}, {v: "disable", t: "No output"}]
                ToggleButton { objectName: "followBothMuted_" + modelData.v; width: 74; height: 22; text: modelData.t
                               active: integration.followBothMuted === modelData.v
                               tip: modelData.v === "disable" ? "The volume keys do nothing until an output is unmuted" : ""
                               onClicked: integration.setFollowBothMuted(modelData.v) } } }
        // Phase 12: byte 59 "Map native volume controls to" (0 none, 1 speakers, 2 headphones)
        Text { width: 330; color: Theme.text; font.pixelSize: 12; text: "Map native volume controls to:" }
        Row { spacing: 6
            Repeater {
                model: [{v: 0, t: "None"}, {v: 1, t: "Speakers"}, {v: 2, t: "Headphones"}]
                ToggleButton { objectName: "volumeMapping" + modelData.v; width: 90; height: 22; text: modelData.t
                               enabled: root.can("set.volumeMapping") && integration.permissions["followOutput"] !== true
                               active: root.v("set.volumeMapping", -1) === modelData.v
                               tip: integration.permissions["followOutput"] === true
                                    ? "Set by \"Follow active output\"; switch that off to choose by hand"
                                    : "Which CONNECT 2 output the system volume (keyboard keys, desktop slider) controls"
                               onClicked: { if (active) return;
                                            if (modelData.v === 2 && !device.simulated) confirmLoud.askValue("set.volumeMapping", 2);
                                            else device.setValue("set.volumeMapping", modelData.v); } } } }
        Text { width: 330; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 10
               text: "macOS streaming mode restarts the device with another USB identity; it is not enabled in this build." }
        ToggleSwitch { width: 330; label: "macOS streaming mode"; enabled: root.can("set.macosStreaming")
                       checked: root.v("set.macosStreaming", false)
                       tip: device.simulated ? "Simulated: restarts the (simulated) device" : "Not enabled on real hardware"
                       onToggled: (b) => confirmMode.ask(b) }
    }
    }
    Flickable {
        objectName: "ledsPage"
        visible: root.open === "leds"
        x: 16; y: root.bodyY; width: 340; height: root.bodyH; clip: true
        contentHeight: ledsCol.height
    Column {
        id: ledsCol
        width: 330; spacing: 10
        Text { color: Theme.text; font.pixelSize: 12
               text: ledFader.dragging && ledFader.commitOnRelease
                     ? "Brightness: " + ledFader.previewValue + " % (release to send; device: " + root.v("set.ledBrightness", 0) + " %)"
                     : "Brightness: " + root.v("set.ledBrightness", 0) + " %" }
        // Simulated device: live while dragging. Real device: one change per release (one settings SET at most),
        // nothing while a change is being sent.
        Fader_h { id: ledFader; objectName: "ledBrightnessFader"; width: 330
                  enabled: root.can("set.ledBrightness") && root.ledWrite !== "pending"
                  commitOnRelease: !device.simulated
                  value: root.v("set.ledBrightness", 0)
                  onMoved: (nv) => device.setValue("set.ledBrightness", nv)
                  onCommitted: (nv) => root.commitLed(nv) }
        Text { objectName: "ledHwCheckWaiting"; width: 330; wrapMode: Text.WordWrap; font.pixelSize: 10; color: Theme.warn
               visible: device.values["hwcheck.state"] === "waiting" && !device.simulated
               text: "Hardware check: no baseline yet — changes are not sent until one is recorded (Settings → Device control)." }
        Text { objectName: "ledWriteStatus"; width: 330; wrapMode: Text.WordWrap; font.pixelSize: 10
               visible: text !== ""; text: root.ledWriteText()
               color: root.ledWrite === "verified" || root.ledWrite === "unchanged" ? Theme.textDim
                    : root.ledWrite === "pending" ? Theme.text : Theme.warn }
        Repeater {
            // what each colour lights on the device (observed on hardware, Phase 11B session 1)
            model: [{k: "icons", t: "Icons", d: "Channel icons of channels that are not muted"},
                    {k: "mute", t: "Mute", d: "Channel icons of muted channels"},
                    {k: "parameter", t: "Parameter", d: "Gain / volume display of the selected channel"},
                    {k: "metering", t: "Metering", d: "Level meter"},
                    {k: "gainReduction", t: "Gain reduction", d: "Gain reduction shown in the level meter (Clipguard / Comp; needs a signal)"}]
            Row {
                id: ledRow
                property var elem: modelData
                readonly property var swatches: ["#ff0000", "#ff8800", "#ffee00", "#00cc44", "#0099ff", "#8844ff", "#ffffff"]
                readonly property string current: String(root.v("set.led." + elem.k, ""))
                // A colour set elsewhere (official app, earlier firmware) that is not one of the swatches is kept
                // here, so it stays selectable and can be restored exactly after trying a swatch.
                property string deviceColour: ""
                function remember() { if (current.length && swatches.indexOf(current) < 0) deviceColour = current; }
                onCurrentChanged: remember()
                Component.onCompleted: remember()
                spacing: 6
                Text { width: 100; color: Theme.text; font.pixelSize: 11; text: ledRow.elem.t
                       anchors.verticalCenter: parent.verticalCenter
                       MouseArea { anchors.fill: parent; hoverEnabled: true
                                   onEntered: Hint.show(ledRow.elem.d); onExited: Hint.clear() } }
                Repeater {
                    model: ledRow.deviceColour.length ? ledRow.swatches.concat([ledRow.deviceColour]) : ledRow.swatches
                    Item {
                        id: sw
                        objectName: "swatch_" + ledRow.elem.k + "_" + index
                        property string key: "set.led." + ledRow.elem.k
                        property string colour: modelData
                        readonly property bool isDeviceColour: index >= ledRow.swatches.length
                        property bool sel: ledRow.current === modelData
                        width: 22; height: 18
                        opacity: root.can(sw.key) ? 1 : 0.4
                        // selection ring: lime outside a dark gap, visible on every swatch colour incl. white
                        Rectangle { anchors.fill: parent; anchors.margins: -3; radius: 5; color: "transparent"
                                    visible: sw.sel; border.width: 2; border.color: "#a4c400" }
                        Rectangle { anchors.fill: parent; radius: 3; color: sw.colour
                                    border.width: 1; border.color: sw.sel ? "#000000" : Theme.line }
                        Text { visible: sw.isDeviceColour; anchors.centerIn: parent; text: "•"; font.pixelSize: 10
                               color: Qt.colorEqual(sw.colour, "#ffffff") ? "#000000" : "#ffffff" }
                        MouseArea { anchors.fill: parent; enabled: root.can(sw.key); hoverEnabled: true
                                    onEntered: if (sw.isDeviceColour) Hint.show("Colour found on the device (" + sw.colour + "), not one of the swatches")
                                    onExited: Hint.clear()
                                    onClicked: device.setValue(sw.key, sw.colour) }
                    }
                }
            }
        }
    }
    }
    Flickable {
        objectName: "buttonPage"
        visible: root.open === "button"
        x: 16; y: root.bodyY; width: 340; height: root.bodyH; clip: true
        contentHeight: buttonCol.height
    Column {
        id: buttonCol
        width: 330; spacing: 8
        Repeater {
            model: [{v: 0, t: "Show CONTROL CENTER"}, {v: 12, t: "Toggle mic preamp styles"},
                    {v: 13, t: "Toggle instrument preamp styles"},
                    {v: 1, t: "Toggle mute: headphone / speakers"},
                    {v: 2, t: "Toggle mute: headphone + mic / speakers"},
                    {v: 8, t: "Dim outputs"}, {v: 9, t: "Mono/stereo outputs"},
                    {v: 14, t: "Toggle brightness settings"}]
            Rectangle {
                id: opt
                property var o: modelData
                width: 330; height: 22; color: "transparent"
                opacity: root.can("set.customButton") ? 1 : 0.55
                Rectangle { id: dot; width: 14; height: 14; radius: 7; color: "transparent"
                            border.color: Theme.textDim; anchors.verticalCenter: parent.verticalCenter
                            Rectangle { anchors.centerIn: parent; width: 8; height: 8; radius: 4; color: Theme.accent
                                        visible: root.v("set.customButton", -1) === opt.o.v } }
                Text { x: 24; anchors.verticalCenter: parent.verticalCenter; color: Theme.text
                       font.pixelSize: 11; text: opt.o.t }
                MouseArea { anchors.fill: parent; enabled: root.can("set.customButton")
                            onClicked: device.setValue("set.customButton", opt.o.v) }
            }
        }
        Text { width: 330; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 10
               text: root.v("set.customButtonKnown", true) ? "Stored value: " + root.v("set.customButton", "?")
                     : "Stored value " + root.v("set.customButton", "?") + " is not offered by the CONNECT 2 UI." }
    }
    }

    // Desktop integration: desktop (KDE/PipeWire) volume sync, background service, vendor fallback,
    // direct output control, kernel compatibility quirk status. Device-writing options need a dialog.
    Flickable {
        id: desktopPage
        objectName: "desktopIntegrationPage"
        visible: root.open === "desktop"
        x: 16; y: root.bodyY; width: 340; height: root.bodyH; clip: true
        contentHeight: desktopCol.height
        onVisibleChanged: if (visible) integration.refresh()
        Column {
            id: desktopCol
            width: 330; spacing: 6
            Text { width: 330; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 11
                   text: !integration.setupAvailable ? "Desktop audio check: not available in this build."
                       : !integration.cardFound ? "The CONNECT 2 is not present as a sound card."
                       : (integration.mixerLoads && integration.volumeControl)
                         ? "Desktop hardware volume: available"
                         : "Desktop hardware volume: not available" }
            Text { width: 330; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 10
                   text: "Kernel compatibility quirk: " + (integration.quirkActive ? "active"
                         : integration.quirkConfigured ? "configured, not active yet (replug the CONNECT 2 or reboot)"
                         : "not configured") }
            Repeater { model: integration.findings
                       Text { width: 330; wrapMode: Text.WordWrap; color: Theme.warn; font.pixelSize: 10; text: "• " + modelData } }
            // Verified on hardware (Phase 11B LB-1): audio flows whenever the device's own input mute is off, while the
            // desktop shows the CONNECT 2 input as muted; the desktop mute switch does not silence it.
            Text { objectName: "desktopMicMuteWarning"; width: 330; wrapMode: Text.WordWrap; font.pixelSize: 10
                   color: integration.softwareInputMute ? Theme.textDim : Theme.warn
                   text: integration.softwareInputMute
                         ? "Desktop input mute: software mute installed - muting the CONNECT 2 input in the desktop silences "
                           + "all apps (the device's own MUTE stays separate)."
                         : "Microphone privacy: the desktop's input mute does not mute the CONNECT 2. Apps hear the "
                           + "microphone and instrument whenever they are unmuted on the device. Use MUTE on the device or in "
                           + "OpenWitty, or install the software input mute (docs/INSTALLATION.md)." }
            Row { spacing: 8
                ToggleButton { width: 150; height: 22; text: quirkHelp.visible ? "Hide quirk instructions" : "About the kernel quirk"
                               onClicked: quirkHelp.visible = !quirkHelp.visible }
                ToggleButton { width: 70; height: 22; text: "Refresh"; onClicked: integration.refresh() } }
            Column { id: quirkHelp; visible: false; width: 330; spacing: 4
                TextEdit { width: 330; wrapMode: Text.WordWrap; readOnly: true; selectByMouse: true
                           color: Theme.textDim; font.pixelSize: 10; text: integration.quirkInstructions }
                ToggleButton { width: 120; height: 22; text: "Copy option line"; onClicked: integration.copyQuirkInstructions() } }

            Rectangle { width: 330; height: 1; color: "#333" }
            ToggleSwitch { width: 330; label: "Show the volume knob in the desktop volume"
                           checked: integration.desktopSync
                           tip: "Keeps the KDE/PipeWire volume in step with the CONNECT 2's speaker volume knob. Never writes the device."
                           onToggled: (b) => integration.setDesktopSync(b) }
            Text { width: 330; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 10
                   text: "Linux cannot see when you turn the speaker volume knob, so OpenWitty passes it on to the "
                       + "desktop volume. The desktop slider and the Mute button already work without this. OpenWitty "
                       + "only sets the desktop volume to the level the device already has, and never unmutes." }
        }
    }

    // Desktop integration, right-hand column: the options that write the device, and the reset.
    Flickable {
        visible: root.open === "desktop"
        x: 390; y: 60; width: 320; height: root.height - 70; clip: true
        contentHeight: desktopRightCol.height
        Column {
            id: desktopRightCol
            width: 310; spacing: 6
            Text { width: 310; color: Theme.text; font.pixelSize: 12; text: "Background" }
            ToggleSwitch { width: 310; label: "Keep it running when OpenWitty is closed"
                           // switching an enabled service OFF is always possible; switching it ON needs desktop sync
                           enabled: integration.serviceState !== "not-installed" && integration.serviceState !== "unknown"
                                    && (integration.desktopSync || checked)
                           checked: integration.serviceState === "enabled" || integration.serviceState === "active"
                           tip: "Starts the openwitty-volumed user service with your desktop session (systemctl --user)."
                           onToggled: (b) => integration.setBackgroundService(b) }
            Text { width: 310; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 10
                   text: "Background service: " + ({"not-installed": "not installed", "disabled": "off",
                         "enabled": "on (not running right now)", "active": "on and running",
                         "unknown": "state unknown"})[integration.serviceState]
                         + ". While this window is open it takes over." }
            Text { visible: integration.serviceIdleOn; width: 310; wrapMode: Text.WordWrap; color: Theme.warn; font.pixelSize: 10
                   text: "Desktop sync is off, so the background service only idles. You can turn it off here." }
            ToggleSwitch { objectName: "trayAutostartSwitch"; width: 310; label: "Show OpenWitty in the system tray"
                           visible: integration.trayInstalled
                           checked: integration.trayAutostart
                           tip: "Adds an OpenWitty icon to the system tray, now and at every login."
                           onToggled: (b) => integration.setTrayAutostart(b) }
            Text { visible: integration.trayInstalled; width: 310; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 10
                   text: "The icon shows whether the knob display and the background service are on, and opens this "
                       + "window. It never uses the device. Closing this window keeps the background service running; "
                       + "\"Quit tray icon\" in its menu removes only the icon." }
            Rectangle { width: 310; height: 1; color: "#333" }
            // Phase 11A.4: separate desktop inputs from the four capture channels (order verified on hardware)
            ToggleSwitch { objectName: "splitSourcesSwitch"; width: 310; label: "Separate inputs for apps"
                           enabled: integration.splitSources !== "unavailable" || checked
                           checked: integration.splitSources === "on" || integration.splitSources === "stale"
                           tip: "Adds \"CONNECT 2 Microphone\", \"CONNECT 2 Instrument\" and \"CONNECT 2 Loopback\" as inputs "
                              + "that apps (voice chat, streaming) can pick directly."
                           onToggled: (b) => integration.setSplitSources(b) }
            Text { objectName: "splitSourcesText"; width: 310; wrapMode: Text.WordWrap; font.pixelSize: 10
                   color: integration.splitSources === "stale" ? Theme.warn : Theme.textDim
                   text: ({"on": "On: Microphone (channel 1), Instrument (2) and Loopback (3/4) are separate inputs.",
                           "off": "Off: apps see one 4-channel CONNECT 2 input (1 microphone, 2 instrument, 3/4 loopback).",
                           "stale": "The separate inputs were set up for another CONNECT 2 input; switch off and on again.",
                           "unavailable": "The CONNECT 2 input is not present in the desktop audio right now."})[integration.splitSources]
                         + (integration.audioRestartNeeded ? " Takes effect after restarting desktop audio." : "") }
            // SR: allow 44.1 / 48 / 96 kHz in PipeWire (otherwise everything is resampled to 48 kHz)
            ToggleSwitch { objectName: "sampleRatesSwitch"; width: 310; label: "Allow 44.1 and 96 kHz for apps (experimental)"
                           checked: integration.extraSampleRates
                           tip: "PipeWire then runs the CONNECT 2 at the rate a DAW or player asks for (44.1, 48 or 96 kHz) "
                              + "instead of converting to 48 kHz. Applies to all sound cards."
                           onToggled: (b) => integration.setExtraSampleRates(b) }
            Text { objectName: "sampleRatesText"; width: 310; wrapMode: Text.WordWrap; font.pixelSize: 10; color: Theme.textDim
                   text: (integration.extraSampleRates ? "On: apps can switch between 44.1, 48 and 96 kHz. At 96 kHz the CONNECT 2 has no Denoise and Comp. "
                                                         + "Warning: repeated rate changes made the CONNECT 2's audio hang until it was unplugged (2026-10-31)."
                                                       : "Off: desktop audio runs at PipeWire's default rate; other rates are converted.")
                         + (integration.liveSampleRates !== "" ? " PipeWire now allows: " + integration.liveSampleRates + "." : "")
                         + (integration.audioRestartNeeded ? " Takes effect after restarting desktop audio." : "") }
            ToggleButton { objectName: "restartAudioButton"; visible: integration.audioRestartNeeded; width: 170; height: 22
                           text: "Restart desktop audio"; onClicked: confirmRestartAudio.visible = true }
            Rectangle { width: 310; height: 1; color: "#333" }
            Text { width: 310; color: Theme.text; font.pixelSize: 12; text: "Background service option that writes the device" }
            ToggleSwitch { width: 310; label: "Vendor fallback (writes the device)"
                           enabled: integration.desktopSync
                           checked: integration.vendorFallback
                           tip: "Only for a device that does not respond to the desktop volume."
                           onToggled: (b) => { if (b) confirmFallback.visible = true; else integration.setVendorFallback(false, false); } }
            Text { width: 310; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 10
                   text: "Only if the desktop reports that the CONNECT 2 did not follow its volume: the background "
                       + "service then applies it through the device's own protocol." }

            Rectangle { width: 310; height: 1; color: "#333" }
            Rectangle { width: 310; height: 1; color: "#333" }
            ToggleButton { width: 170; height: 22; text: "Reset desktop integration"; onClicked: confirmReset.visible = true }
            Text { visible: integration.lastError.length > 0; width: 310; wrapMode: Text.WordWrap
                   color: Theme.danger; font.pixelSize: 10; text: integration.lastError }
            Text { width: 310; wrapMode: Text.WrapAnywhere; color: Theme.textDim; font.pixelSize: 9
                   text: "Settings file: " + integration.configPath }
            Repeater { model: integration.configWarnings
                       Text { width: 310; wrapMode: Text.WordWrap; color: Theme.warn; font.pixelSize: 9; text: "Settings: " + modelData } }
        }
    }

    // Device control (Phase 10B): the independent permissions that let this window write the real CONNECT 2.
    Flickable {
        objectName: "deviceInfoPage"
        visible: root.open === "device"
        x: 16; y: root.bodyY; width: 340; height: root.bodyH; clip: true
        contentHeight: deviceCol.height
    Column {
        id: deviceCol
        width: 330; spacing: 8
        Text { width: 330; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 11
               text: "On the real CONNECT 2 every control is display-only unless you allow it here. Each permission is "
                   + "separate, off by default and covers only its own setting." }
        Text { objectName: "deviceControlSummary"; width: 330; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 10
               text: "Currently allowed: " + (device.simulated ? "everything (simulated device)" : root.authorizedSummary()) }
    }
    }
    Flickable {
        objectName: "deviceControlPage"
        visible: root.open === "device"
        x: 390; y: 60; width: 320; height: root.height - 70; clip: true
        contentHeight: deviceRightCol.height
        Column {
            id: deviceRightCol
            width: 310; spacing: 6
            Text { width: 310; color: Theme.text; font.pixelSize: 12; text: "Options that write the device" }
            Text { visible: integration.legacyOutputControlPending; width: 310; wrapMode: Text.WordWrap
                   objectName: "legacyOutputNotice"; color: Theme.warn; font.pixelSize: 10
                   text: "An older OpenWitty had one combined speaker/headphone permission. It no longer grants anything: "
                       + "turn on the speaker and/or headphone control below if you want them." }
            ToggleButton { visible: integration.legacyOutputControlPending; width: 90; height: 20; text: "Dismiss"
                           onClicked: integration.dismissLegacyOutputControl() }
            ToggleSwitch { objectName: "speakerControlSwitch"; width: 310; label: "Speaker volume and mute"
                           checked: integration.speakerControl
                           tip: "Lets this window change the speaker volume and mute (bytes 20-22) - nothing else."
                           onToggled: (b) => { if (b) confirmWrite.ask("speaker"); else integration.setSpeakerControl(false, false); } }
            ToggleSwitch { objectName: "headphoneControlSwitch"; width: 310; label: "Headphone volume and mute"
                           checked: integration.headphoneControl
                           tip: "Lets this window change the headphone volume and mute (bytes 27-29) - nothing else."
                           onToggled: (b) => { if (b) confirmWrite.ask("headphone"); else integration.setHeadphoneControl(false, false); } }
            ToggleSwitch { objectName: "ledControlSwitch"; width: 310; label: "LED brightness"
                           checked: integration.ledBrightnessControl
                           tip: "Lets this window change the LED brightness (byte 37) - nothing else."
                           onToggled: (b) => { if (b) confirmWrite.ask("led"); else integration.setLedBrightnessControl(false, false); } }
            Repeater {
                model: root.extraPermissions
                ToggleSwitch { objectName: "permSwitch_" + modelData.n; width: 310; label: modelData.label
                               checked: integration.permissions[modelData.n] === true
                               tip: integration.permissionDescription(modelData.n)
                               onToggled: (b) => { if (b) confirmWrite.ask(modelData.n); else integration.setPermission(modelData.n, false, false); } }
            }
            Text { width: 310; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 10
                   text: "Each permission is separate, off by default and changes only its own setting. Every change is "
                       + "read back; volume increases are limited to 10 dB per step. Separate from desktop sync." }
            Text { objectName: "writeLockedNotice"; width: 310; wrapMode: Text.WordWrap; color: Theme.warn; font.pixelSize: 10
                   visible: device.values["write.locked"] === true
                   text: "Device writes stopped: " + (device.values["write.lockReason"] || "") + "." }
            // Opt-in (openwitty --hw-check): read-only snapshots for an owner-approved hardware test.
            Column {
                id: hwPanel
                objectName: "hwCheckPanel"; visible: device.values["hwcheck.enabled"] === true; width: 310; spacing: 4
                readonly property bool hasBaseline: device.values["hwcheck.state"] === "recorded"
                property bool replaceArmed: false
                Text { width: 310; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 11
                       text: "Hardware check (read-only, this window's own device connection). Baseline LED brightness: "
                           + (hwPanel.hasBaseline ? device.values["hwcheck.baseline"] + " %"
                              + (device.values["hwcheck.replacements"] > 0 ? " (replaced " + device.values["hwcheck.replacements"] + "×)" : "")
                              : "not recorded") }
                Text { objectName: "hwCheckNoBaseline"; width: 310; wrapMode: Text.WordWrap; color: Theme.warn; font.pixelSize: 10
                       visible: !hwPanel.hasBaseline
                       text: "No baseline yet: device writes are blocked until the first successful settings read is recorded." }
                Row { spacing: 6; visible: !hwPanel.replaceArmed
                    ToggleButton { objectName: "hwCheckBaselineButton"; width: 110; height: 20
                                   text: hwPanel.hasBaseline ? "Replace baseline…" : "Record baseline"
                                   onClicked: { if (hwPanel.hasBaseline) hwPanel.replaceArmed = true; else device.hwCheckRecordBaseline(); } }
                    ToggleButton { objectName: "hwCheckCompareButton"; width: 130; height: 20; text: "Compare with baseline"
                                   enabled: hwPanel.hasBaseline; onClicked: device.hwCheckCompare() } }
                Row { spacing: 6; visible: hwPanel.replaceArmed
                    Text { width: 150; wrapMode: Text.WordWrap; color: Theme.warn; font.pixelSize: 10
                           text: "Replace the test's reference with the device's current settings?" }
                    ToggleButton { objectName: "hwCheckReplaceConfirm"; width: 70; height: 20; text: "Replace"; primary: true
                                   onClicked: { hwPanel.replaceArmed = false; device.hwCheckReplaceBaseline(); } }
                    ToggleButton { objectName: "hwCheckReplaceCancel"; width: 60; height: 20; text: "Cancel"
                                   onClicked: hwPanel.replaceArmed = false } }
                Text { objectName: "hwCheckReport"; width: 310; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 10
                       text: device.values["hwcheck.report"] || "" }
            }

        }
    }

    // right column
    Column {
        visible: root.open !== "desktop" && root.open !== "device"
        x: 390; y: 60; width: 310; spacing: 10
        Text { width: 310; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 12; text: "OpenWitty " + Qt.application.version + " — unofficial, experimental; not affiliated with LEWITT" }
        Text { color: Theme.textDim; font.pixelSize: 12
               text: "CONNECT 2 firmware version: " + (device.simulated ? "simulated" : "not decoded yet") }
        Row { spacing: 8
            ToggleButton { width: 120; height: 26; text: "Check for updates"; primary: true; enabled: false; tip: "Not implemented" }
            ToggleButton { width: 100; height: 26; text: "Manual update"; enabled: false; tip: "Firmware update is out of scope" } }
        Text { width: 310; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 10
               text: "Firmware update and factory reset are disabled: they are destructive/persistent and not validated." }
        Rectangle { width: 310; height: 1; color: "#333" }
        ToggleSwitch { objectName: "showTooltipsSwitch"; width: 310; label: "Show tooltips"; checked: Hint.enabled
                       onToggled: (b) => { Hint.enabled = b; if (!b) Hint.clear();
                                           if (typeof prefs !== "undefined") prefs.setShowTooltips(b); } }
        ToggleSwitch { width: 310; label: "Retrigger onboarding"; checked: false
                       onToggled: (b) => { if (b) root.onboardingRequested(); } }
    }
    ToggleButton { anchors.right: parent.right; anchors.rightMargin: 16; anchors.bottom: parent.bottom
                   visible: root.open !== "desktop" && root.open !== "device"
                   anchors.bottomMargin: 14; width: 100; text: "Factory reset"; enabled: false
                   tip: "Disabled by policy" }

    Rectangle {
        id: confirmMode
        visible: false; z: 20; anchors.centerIn: parent; width: 300; height: 120; radius: 6
        color: Theme.panelHi; border.color: Theme.warn
        property bool target: false
        function ask(on) { target = on; visible = true; }
        Column { anchors.centerIn: parent; spacing: 10
            Text { width: 270; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 11
                   text: (confirmMode.target ? "Turning on" : "Turning off") + " macOS streaming mode will restart your USB device. All current settings will be preserved. Do you want to continue?" }
            Row { spacing: 8; anchors.horizontalCenter: parent.horizontalCenter
                ToggleButton { width: 70; text: "Cancel"; onClicked: confirmMode.visible = false }
                ToggleButton { width: 70; text: "Continue"; primary: true
                               onClicked: { confirmMode.visible = false; device.setDeviceMode(confirmMode.target); } } }
        }
    }

    Rectangle {
        id: confirmFallback
        visible: false; z: 20; anchors.centerIn: parent; width: 320; height: 170; radius: 6
        color: Theme.panelHi; border.color: Theme.warn
        Column { anchors.centerIn: parent; spacing: 10
            Text { width: 290; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 11
                   text: "The vendor fallback writes the CONNECT 2's speaker volume and mute when the device ignores a "
                       + "desktop change. Volume increases are limited to 10 dB per step, nothing is written at start-up, "
                       + "and every write is read back. Use it only if the desktop volume does not reach the speaker." }
            Row { spacing: 8; anchors.horizontalCenter: parent.horizontalCenter
                ToggleButton { width: 70; text: "Cancel"; onClicked: confirmFallback.visible = false }
                ToggleButton { width: 70; text: "Enable"; active: true; activeColor: Theme.warn
                               onClicked: { confirmFallback.visible = false; integration.setVendorFallback(true, true); } } }
        }
    }

    Rectangle {
        id: confirmWrite
        objectName: "confirmWriteDialog"
        visible: false; z: 20; anchors.centerIn: parent; width: 330; radius: 6
        color: Theme.panelHi; border.color: Theme.warn
        height: Math.max(160, confirmCol.height + 30)
        property string field: ""
        function ask(f) { field = f; visible = true; }
        function accept() {
            visible = false;
            if (field === "speaker") integration.setSpeakerControl(true, true);
            else if (field === "headphone") integration.setHeadphoneControl(true, true);
            else if (field === "led") integration.setLedBrightnessControl(true, true);
            else integration.setPermission(field, true, true);
        }
        function extraText(f) {
            for (var i = 0; i < root.extraPermissions.length; ++i)
                if (root.extraPermissions[i].n === f) return root.extraPermissions[i].confirm;
            return "";
        }
        Column { id: confirmCol; anchors.centerIn: parent; spacing: 10
            Text { width: 300; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 11
                   text: confirmWrite.field === "speaker"
                         ? "OpenWitty will be able to change the CONNECT 2's speaker volume and mute - only those. "
                           + "Each change is read back; it can get at most 10 dB louder per step. Turn your monitors down first."
                         : confirmWrite.field === "headphone"
                         ? "OpenWitty will be able to change the CONNECT 2's headphone volume and mute - only those. "
                           + "Each change is read back; it can get at most 10 dB louder per step. Take your headphones off first."
                         : confirmWrite.field === "led"
                         ? "OpenWitty will be able to change the CONNECT 2's LED brightness - only that, no colours and "
                           + "no audio setting. Each change is read back."
                         : confirmWrite.extraText(confirmWrite.field) }
            Row { spacing: 8; anchors.horizontalCenter: parent.horizontalCenter
                ToggleButton { objectName: "confirmWriteCancel"; width: 70; text: "Cancel"; onClicked: confirmWrite.visible = false }
                ToggleButton { objectName: "confirmWriteEnable"; width: 70; text: "Enable"; active: true; activeColor: Theme.warn
                               onClicked: confirmWrite.accept() } }
        }
    }

    Rectangle {
        id: confirmReset
        visible: false; z: 20; anchors.centerIn: parent; width: 300; height: 140; radius: 6
        color: Theme.panelHi; border.color: Theme.warn
        Column { anchors.centerIn: parent; spacing: 10
            Text { width: 270; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 11
                   text: "Turn off desktop volume sync, the vendor fallback, every device-control permission, the background "
                       + "service and the system tray icon? The kernel quirk, if you installed it, is not touched." }
            Row { spacing: 8; anchors.horizontalCenter: parent.horizontalCenter
                ToggleButton { width: 70; text: "Cancel"; onClicked: confirmReset.visible = false }
                ToggleButton { width: 70; text: "Reset"; primary: true
                               onClicked: { confirmReset.visible = false; integration.resetToDefaults(); } } }
        }
    }

    Rectangle {
        id: confirmRestartAudio
        objectName: "confirmRestartAudioDialog"
        visible: false; z: 20; anchors.centerIn: parent; width: 300; height: 130; radius: 6
        color: Theme.panelHi; border.color: Theme.warn
        Column { anchors.centerIn: parent; spacing: 10
            Text { width: 270; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 11
                   text: "Restart PipeWire and WirePlumber now? All sound stops for a moment and apps that are recording or "
                       + "playing may need to reconnect. Levels are restored as at login." }
            Row { spacing: 8; anchors.horizontalCenter: parent.horizontalCenter
                ToggleButton { width: 70; text: "Cancel"; onClicked: confirmRestartAudio.visible = false }
                ToggleButton { objectName: "confirmRestartAudioButton"; width: 70; text: "Restart"; primary: true
                               onClicked: { confirmRestartAudio.visible = false; integration.restartAudio(true); } } }
        }
    }

    Rectangle {
        id: confirmLoud
        visible: false; z: 20; anchors.centerIn: parent; width: 260; height: 140; radius: 6
        color: Theme.panelHi; border.color: Theme.warn
        property string key: "set.highVolume"
        property var enableValue: true
        objectName: "confirmLoudDialog"
        function ask(k, on) { key = k; enableValue = true; if (!on) device.setValue(k, false); else visible = true; }
        function askValue(k, value) { key = k; enableValue = value; visible = true; }
        Column { anchors.centerIn: parent; spacing: 10
            Text { width: 230; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 11
                   text: confirmLoud.key === "set.highImpedance"
                         ? "High-impedance headphone mode raises the headphone output level. Take your headphones off first."
                         : confirmLoud.key === "set.mixToSpeakers"
                         ? "The speakers will also play the microphone and instrument. A microphone near the speakers can feed back: lower your monitors first."
                         : confirmLoud.key === "set.volumeMapping"
                         ? "The keyboard volume keys and the desktop slider will set the headphone volume directly. Take your headphones off and check the level first."
                         : confirmLoud.key === "set.mobileStreaming"
                         ? "Software inputs 1/2 will carry the microphone + instrument mix instead of separate inputs. Recordings and calls change."
                         : "High volume speaker mode raises the maximum speaker output level. Lower your monitors first." }
            Row { spacing: 8; anchors.horizontalCenter: parent.horizontalCenter
                ToggleButton { width: 70; text: "Cancel"; onClicked: confirmLoud.visible = false }
                ToggleButton { width: 70; text: "Enable"; active: true; activeColor: Theme.warn
                               onClicked: { confirmLoud.visible = false; device.setValue(confirmLoud.key, confirmLoud.enableValue); } } }
        }
    }
}
