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
    // Debug mode (openwitty --debug): hardware-test tools and experimental options.
    readonly property bool debug: typeof debugMode !== "undefined" && debugMode === true
    property string open: "audio"
    // Phase 12D: this device's extended custom-button mode is in effect (stored for this device and its base is on it)
    readonly property string followKey: { var k = root.v("device.followKey", ""); return k === "simulated" ? "" : k; }
    readonly property bool buttonExtended: followKey !== "" && integration.buttonFollow[followKey] !== undefined
                                           && integration.buttonFollow[followKey] === root.v("set.customButton", -1)
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
    Text { x: 16; y: 12; text: "Settings"; color: Theme.text; font.pixelSize: 15 }
    ToggleButton { anchors.right: parent.right; anchors.rightMargin: 16; y: 10; width: 60; text: "Close"
                   onClicked: root.closeRequested() }
    Rectangle { x: 0; y: 44; width: parent.width; height: 1; color: Theme.line }

    // Accordion: headers up to the open one at the top, its section directly below it, the remaining headers at the
    // bottom of the column.
    readonly property var sectionModel: {
        var m = [{k: "audio", t: "Audio"}, {k: "leds", t: "LEDs"}, {k: "button", t: "Custom button"},
                 {k: "presets", t: "Presets"}, {k: "desktop", t: "Desktop integration"}];
        if (root.debug) m.push({k: "debug", t: "Debug"});
        return m;
    }
    readonly property var sections: sectionModel.map(function(s) { return s.k; })
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
            model: root.sectionModel
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
                       tip: "The speakers play the monitoring mix, not only the computer audio"
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
                       tip: "Inputs 1 and 2 both carry the microphone + instrument mix"
                       onToggled: (b) => confirmLoud.ask("set.mobileStreaming", b) }
        // Phase 12C: follow active output (owner spec): OpenWitty keeps byte 59 on the audible output
        ToggleSwitch { objectName: "followOutputSwitch"; width: 330; label: "Follow active output"
                       enabled: !root.buttonExtended
                       checked: integration.followActiveOutput || root.buttonExtended
                       tip: "The volume keys control the output you can hear"
                       onToggled: (b) => integration.setFollowActiveOutput(b) }
        Text { objectName: "followRequiredNote"; width: 330; wrapMode: Text.WordWrap; color: Theme.warn; font.pixelSize: 10
               visible: root.buttonExtended
               text: "On because the custom button is set to \"switch outputs + follow volume\". Choose another custom "
                     + "button function to change this." }
        Text { width: 330; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 10
               visible: integration.followActiveOutput || root.buttonExtended
               text: "Speakers on only: speakers. Headphones on only: headphones. Needs desktop volume sync (Desktop integration)."
                     + (integration.desktopSync ? "" : " Desktop volume sync is off: nothing follows right now.") }
        ToggleSwitch { objectName: "followFocusSwitch"; width: 330; label: "Also move the touch-wheel focus"
                       visible: integration.followActiveOutput || root.buttonExtended
                       checked: integration.followMoveFocus
                       tip: "The CONNECT 2's touch wheel controls the same output"
                       onToggled: (b) => integration.setFollowMoveFocus(b) }
        Text { width: 330; color: Theme.text; font.pixelSize: 11; visible: integration.followActiveOutput || root.buttonExtended
               text: "When both outputs are on, the volume keys control:" }
        Row { spacing: 6; visible: integration.followActiveOutput || root.buttonExtended
            Repeater {
                model: [{v: "speakers", t: "Speakers"}, {v: "headphones", t: "Headphones"}, {v: "keep", t: "Keep last"}]
                ToggleButton { objectName: "followBothOn_" + modelData.v; width: 90; height: 22; text: modelData.t
                               active: integration.followBothOn === modelData.v
                               onClicked: integration.setFollowBothOn(modelData.v) } } }
        Text { width: 330; color: Theme.text; font.pixelSize: 11; visible: integration.followActiveOutput || root.buttonExtended
               text: "When both outputs are muted:" }
        Row { spacing: 6; visible: integration.followActiveOutput || root.buttonExtended
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
                               enabled: root.can("set.volumeMapping") && !integration.followActiveOutput && !root.buttonExtended
                               active: root.v("set.volumeMapping", -1) === modelData.v
                               tip: integration.followActiveOutput
                                    ? "Set by \"Follow active output\"; switch that off to choose by hand"
                                    : "Which CONNECT 2 output the system volume (keyboard keys, desktop slider) controls"
                               onClicked: { if (active) return;
                                            if (modelData.v === 2 && !device.simulated) confirmLoud.askValue("set.volumeMapping", 2);
                                            else device.setValue("set.volumeMapping", modelData.v); } } } }
        Text { width: 330; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 10; visible: device.simulated || root.debug
               text: "macOS streaming mode restarts the device with another USB identity; it is not enabled in this build." }
        ToggleSwitch { width: 330; label: "macOS streaming mode"; enabled: root.can("set.macosStreaming"); visible: device.simulated || root.debug
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
            model: [{k: "icons", t: "Icons", d: "Channel icons, seen on the selected channel; a muted channel shows the Mute colour instead"},
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
            // x: OpenWitty extension (Phase 12D): the native base function plus "follow active output" for this device
            model: [{v: 0, t: "Show CONTROL CENTER"}, {v: 12, t: "Toggle mic preamp styles"},
                    {v: 13, t: "Toggle instrument preamp styles"},
                    {v: 1, t: "Toggle mute: headphone / speakers"},
                    {v: 1, x: true, t: "Toggle mute: headphone / speakers + follow volume"},
                    {v: 2, t: "Toggle mute: headphone + mic / speakers"},
                    {v: 2, x: true, t: "Toggle mute: hp + mic / speakers + follow volume"},
                    {v: 8, t: "Dim outputs"}, {v: 9, t: "Mono/stereo outputs"},
                    {v: 14, t: "Toggle brightness settings"}]
            Rectangle {
                id: opt
                property var o: modelData
                width: 330; height: 22; color: "transparent"
                opacity: (root.can("set.customButton") || root.v("set.customButton", -1) === opt.o.v) ? 1 : 0.55
                Rectangle { id: dot; width: 14; height: 14; radius: 7; color: "transparent"
                            border.color: Theme.textDim; anchors.verticalCenter: parent.verticalCenter
                            Rectangle { anchors.centerIn: parent; width: 8; height: 8; radius: 4; color: Theme.accent
                                        visible: root.v("set.customButton", -1) === opt.o.v
                                                 && (opt.o.x === true) === root.buttonExtended } }
                Text { x: 24; width: 306; elide: Text.ElideRight; anchors.verticalCenter: parent.verticalCenter
                       color: opt.o.x ? Theme.accent : Theme.text; font.pixelSize: 11; text: opt.o.t }
                MouseArea { id: optHover; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton
                            onEntered: if (opt.o.x === true) Hint.show("OpenWitty extension: the volume keys also follow the output you hear")
                            onExited: Hint.clear() }
                // the base already on the device: only OpenWitty's setting changes, no device write (no permission needed)
                MouseArea { anchors.fill: parent; enabled: root.can("set.customButton") || root.v("set.customButton", -1) === opt.o.v
                            onClicked: {
                                if (opt.o.x) { confirmButtonFollow.ask(opt.o.v); return; }
                                if (root.followKey !== "") integration.setButtonFollow(root.followKey, -1, false);
                                if (root.v("set.customButton", -1) !== opt.o.v) device.setValue("set.customButton", opt.o.v);
                            } }
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
                           tip: "The desktop volume follows the CONNECT 2's speaker knob"
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
                           tip: "Starts OpenWitty's background service with your desktop session"
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
                           tip: "Shows an OpenWitty icon in the system tray, also after login"
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
                           tip: "Microphone, Instrument and Loopback as separate inputs for apps"
                           onToggled: (b) => integration.setSplitSources(b) }
            Text { objectName: "splitSourcesText"; width: 310; wrapMode: Text.WordWrap; font.pixelSize: 10
                   color: integration.splitSources === "stale" ? Theme.warn : Theme.textDim
                   text: ({"on": "On: Microphone (channel 1), Instrument (2) and Loopback (3/4) are separate inputs.",
                           "off": "Off: apps see one 4-channel CONNECT 2 input (1 microphone, 2 instrument, 3/4 loopback).",
                           "stale": "The separate inputs were set up for another CONNECT 2 input; switch off and on again.",
                           "unavailable": "The CONNECT 2 input is not present in the desktop audio right now."})[integration.splitSources]
                         + (integration.audioRestartNeeded ? " Takes effect after restarting desktop audio." : "") }
            // Readable channel names (JACK pretty names) for the CONNECT 2 ports, display only
            ToggleSwitch { objectName: "nameChannelsSwitch"; width: 310; label: "Name the CONNECT 2 channels in audio apps"
                           checked: integration.nameChannels
                           tip: "Audio apps show 1 Microphone, 2 Instrument, 3/4 Loopback (labels only)"
                           onToggled: (b) => integration.setNameChannels(b) }
            // SR: allow 44.1 / 48 / 96 kHz in PipeWire (otherwise everything is resampled to 48 kHz)
            ToggleSwitch { objectName: "sampleRatesSwitch"; width: 310; label: "Allow 44.1 and 96 kHz for apps (experimental)"
                           visible: root.debug || integration.extraSampleRates
                           checked: integration.extraSampleRates
                           tip: "Apps may switch between 44.1, 48 and 96 kHz (all sound cards)"
                           onToggled: (b) => integration.setExtraSampleRates(b) }
            Text { objectName: "sampleRatesText"; width: 310; wrapMode: Text.WordWrap; font.pixelSize: 10; color: Theme.textDim
                   visible: root.debug || integration.extraSampleRates
                   text: (integration.extraSampleRates ? "On: apps can switch between 44.1, 48 and 96 kHz. At 96 kHz the CONNECT 2 has no Denoise and Comp. "
                                                         + "Warning: repeated rate changes made the CONNECT 2's audio hang until it was unplugged (2026-10-31)."
                                                       : "Off: desktop audio runs at PipeWire's default rate; other rates are converted.")
                         + (integration.liveSampleRates !== "" ? " PipeWire now allows: " + integration.liveSampleRates + "." : "")
                         + (integration.audioRestartNeeded ? " Takes effect after restarting desktop audio." : "") }
            ToggleButton { objectName: "restartAudioButton"; visible: integration.audioRestartNeeded; width: 170; height: 22
                           text: "Restart desktop audio"; onClicked: confirmRestartAudio.visible = true }
            Rectangle { width: 310; height: 1; color: "#333"; visible: root.debug || integration.vendorFallback }
            Text { width: 310; color: Theme.text; font.pixelSize: 12; text: "Experimental: vendor fallback"
                   visible: root.debug || integration.vendorFallback }
            ToggleSwitch { objectName: "vendorFallbackSwitch"; width: 310; label: "Vendor fallback (writes the device)"
                           visible: root.debug || integration.vendorFallback
                           enabled: integration.desktopSync
                           checked: integration.vendorFallback
                           tip: "Only for a device that does not respond to the desktop volume."
                           onToggled: (b) => { if (b) confirmFallback.visible = true; else integration.setVendorFallback(false, false); } }
            Text { width: 310; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 10
                   visible: root.debug || integration.vendorFallback
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

    // Debug (openwitty --debug): hardware-test tools; nothing here is needed for normal use.
    Flickable {
        objectName: "debugInfoPage"
        visible: root.open === "debug"
        x: 16; y: root.bodyY; width: 340; height: root.bodyH; clip: true
        contentHeight: debugCol.height
    Column {
        id: debugCol
        width: 330; spacing: 8
        Text { width: 330; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 11
               text: "Debug mode: tools for hardware tests and experimental options. Not needed for normal use; "
                   + "start OpenWitty normally to hide this section." }
        Text { width: 330; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 10
               text: "In debug mode, Desktop integration also shows the experimental options: vendor fallback and "
                   + "44.1 / 96 kHz for apps." }
        Text { objectName: "debugLastWrite"; width: 330; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 10
               text: "Last device change: " + (device.values["write.key"] ? device.values["write.key"] + " - "
                     + (device.values["write.outcome"] || "") + (device.values["write.message"] ? " (" + device.values["write.message"] + ")" : "")
                     : "none yet") }
        Text { width: 330; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 10
               text: "Connection: " + (device.backend || "none") + (device.simulated ? " (simulated)" : "") }
    }
    }
    Flickable {
        objectName: "debugToolsPage"
        visible: root.open === "debug"
        x: 390; y: 60; width: 320; height: root.height - 70; clip: true
        contentHeight: deviceRightCol.height
        Column {
            id: deviceRightCol
            width: 310; spacing: 6
            Text { width: 310; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 10
                   visible: device.values["hwcheck.enabled"] !== true
                   text: "Hardware check: start with openwitty --hw-check to record a settings baseline and compare "
                       + "the device after each change." }
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

    // Presets (P3): OpenWitty's own preset files; loading shows every change and asks first.
    Flickable {
        id: presetsPage
        objectName: "presetsPage"
        visible: root.open === "presets"
        x: 16; y: root.bodyY; width: 340; height: root.bodyH; clip: true
        contentHeight: presetCol.height
        readonly property bool available: typeof presets !== "undefined"
        property string status: ""
        property string replaceName: ""
        property string deleteFile: ""
        readonly property string applyMessage: device.values["preset.message"] || ""
        onApplyMessageChanged: if (applyMessage !== "") status = applyMessage.charAt(0).toUpperCase() + applyMessage.slice(1) + "."
        function save(overwrite) {
            var err = presets.save(presetName.text, presetOutputs.checked, overwrite);
            replaceName = "";
            status = err === "" ? "Saved \"" + presetName.text.trim() + "\"." : err;
        }
        Column {
            id: presetCol
            width: 330; spacing: 8
            Text { width: 330; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 10
                   text: "Save the current input, processing, monitoring, LED and button settings and load them later. "
                       + "Loading shows every change first; each one is sent and read back on its own." }
            Row { spacing: 6
                Rectangle { width: 200; height: 24; radius: 2; color: Theme.well; border.color: Theme.line
                    TextInput { id: presetName; objectName: "presetNameInput"; anchors.fill: parent; anchors.margins: 5
                                color: Theme.text; font.pixelSize: 12; clip: true; maximumLength: 64
                                selectByMouse: true; enabled: presetsPage.available
                                Text { visible: parent.text === ""; color: Theme.textDim; font.pixelSize: 12; text: "Preset name" } } }
                ToggleButton { objectName: "presetSaveButton"; width: 124; height: 24; text: "Save current"; primary: true
                               enabled: presetsPage.available && presetName.text.trim() !== "" && device.values["in1.gain"] !== undefined
                               onClicked: { if (presets.exists(presetName.text)) presetsPage.replaceName = presetName.text.trim();
                                            else presetsPage.save(false); } } }
            Row { spacing: 6; visible: presetsPage.replaceName !== ""
                Text { width: 180; wrapMode: Text.WordWrap; color: Theme.warn; font.pixelSize: 10
                       text: "\"" + presetsPage.replaceName + "\" exists. Replace it?" }
                ToggleButton { objectName: "presetReplaceButton"; width: 70; height: 20; text: "Replace"; primary: true
                               onClicked: presetsPage.save(true) }
                ToggleButton { width: 60; height: 20; text: "Cancel"; onClicked: presetsPage.replaceName = "" } }
            ToggleSwitch { id: presetOutputs; objectName: "presetOutputsSwitch"; width: 330
                           label: "Include speaker and headphone levels"; checked: false
                           tip: "Off: loading the preset never changes your output volumes" }
            Text { objectName: "presetStatus"; width: 330; wrapMode: Text.WordWrap; font.pixelSize: 10
                   visible: presetsPage.status !== ""
                   color: device.values["preset.state"] === "stopped" ? Theme.warn : Theme.textDim
                   text: device.values["preset.state"] === "applying"
                         ? "Applying… " + device.values["preset.done"] + " / " + device.values["preset.total"]
                         : presetsPage.status }
        }
    }

    // Presets: the saved presets, in the right column
    Flickable {
        objectName: "presetListPage"
        visible: root.open === "presets"
        x: 390; y: 60; width: Math.min(360, root.width - 406); height: root.height - 76; clip: true
        contentHeight: presetListCol.height
        Column {
            id: presetListCol
            width: parent.width - 6; spacing: 6
            Text { color: Theme.text; font.pixelSize: 12; text: "Saved presets" }
            Text { width: 330; color: Theme.textDim; font.pixelSize: 11
                   visible: !presetsPage.available || presets.list.length === 0; text: "No presets saved yet." }
            Repeater {
                model: presetsPage.available ? presets.list : []
                Rectangle {
                    width: presetListCol.width; height: 30; radius: 2; color: Theme.panel
                    objectName: "presetRow_" + modelData.file
                    Column { x: 8; anchors.verticalCenter: parent.verticalCenter
                        Text { width: presetListCol.width - 150; elide: Text.ElideRight; color: Theme.text; font.pixelSize: 12; text: modelData.name }
                        Text { color: Theme.textDim; font.pixelSize: 9; text: modelData.modified } }
                    Row { anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter; spacing: 4
                        visible: presetsPage.deleteFile !== modelData.file
                        ToggleButton { objectName: "presetLoad_" + modelData.file; width: 60; height: 22; text: "Load…"; primary: true
                                       enabled: device.values["preset.state"] !== "applying"
                                       onClicked: presetConfirm.ask(modelData.file) }
                        ToggleButton { width: 60; height: 22; text: "Delete"; onClicked: presetsPage.deleteFile = modelData.file } }
                    Row { anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter; spacing: 4
                        visible: presetsPage.deleteFile === modelData.file
                        Text { anchors.verticalCenter: parent.verticalCenter; color: Theme.warn; font.pixelSize: 10; text: "Delete?" }
                        ToggleButton { width: 50; height: 22; text: "Yes"; active: true; activeColor: Theme.warn
                                       onClicked: { presets.remove(modelData.file); presetsPage.deleteFile = ""; } }
                        ToggleButton { width: 40; height: 22; text: "No"; onClicked: presetsPage.deleteFile = "" } }
                }
            }
            Row { spacing: 6
                ToggleButton { width: 140; height: 22; text: "Open presets folder"; enabled: presetsPage.available
                               tip: "Copy .owpreset files here to import them, or out of here to share them"
                               onClicked: presets.openFolder() }
                ToggleButton { width: 70; height: 22; text: "Refresh"; enabled: presetsPage.available; onClicked: presets.refresh() } }
        }
    }

    // right column
    Column {
        visible: root.open !== "desktop" && root.open !== "debug" && root.open !== "presets"
        x: 390; y: 60; width: 310; spacing: 10
        Text { width: 310; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 12; text: "OpenWitty " + Qt.application.version + " — unofficial, experimental; not affiliated with LEWITT" }
        Text { color: Theme.textDim; font.pixelSize: 12
               text: "CONNECT 2 firmware version: " + (device.simulated ? "simulated"
                     : (root.v("device.usbRevision", "") !== "" && root.v("device.usbRevision", "") !== "-")
                       ? root.v("device.usbRevision", "") + " (USB revision)" : "unknown") }
        Text { width: 310; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 10
               text: "Firmware update and factory reset are not part of OpenWitty; use LEWITT's official app for them." }
        Rectangle { width: 310; height: 1; color: "#333" }
        ToggleSwitch { objectName: "showTooltipsSwitch"; width: 310; label: "Show tooltips"; checked: Hint.enabled
                       onToggled: (b) => { Hint.enabled = b; if (!b) Hint.clear();
                                           if (typeof prefs !== "undefined") prefs.setShowTooltips(b); } }
        ToggleSwitch { width: 310; label: "Retrigger onboarding"; checked: false
                       onToggled: (b) => { if (b) root.onboardingRequested(); } }
    }

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
        id: confirmButtonFollow
        objectName: "confirmButtonFollowDialog"
        visible: false; z: 20; anchors.centerIn: parent; width: 330; height: 150; radius: 6
        color: Theme.panelHi; border.color: Theme.warn
        property int base: 1
        function ask(b) { base = b; visible = true; }
        Column { anchors.centerIn: parent; spacing: 10
            Text { width: 300; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 11
                   text: "The custom button will switch the outputs" + (confirmButtonFollow.base === 2 ? " and the mic" : "")
                         + " as usual, and OpenWitty will move the system volume keys (and the touch-wheel focus) to the "
                         + "output you can hear - also from the background service. Remembered for this CONNECT 2." }
            Row { spacing: 8; anchors.horizontalCenter: parent.horizontalCenter
                ToggleButton { width: 70; text: "Cancel"; onClicked: confirmButtonFollow.visible = false }
                ToggleButton { objectName: "confirmButtonFollowButton"; width: 70; text: "Enable"; active: true; activeColor: Theme.warn
                               onClicked: {
                                   confirmButtonFollow.visible = false;
                                   if (root.followKey !== "") integration.setButtonFollow(root.followKey, confirmButtonFollow.base, true);
                                   if (root.v("set.customButton", -1) !== confirmButtonFollow.base)
                                       device.setValue("set.customButton", confirmButtonFollow.base);
                               } } }
        }
    }

    Rectangle {
        id: confirmReset
        visible: false; z: 20; anchors.centerIn: parent; width: 300; height: 140; radius: 6
        color: Theme.panelHi; border.color: Theme.warn
        Column { anchors.centerIn: parent; spacing: 10
            Text { width: 270; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 11
                   text: "Turn off desktop volume sync, follow active output, the vendor fallback, the background "
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
    Rectangle {
        id: presetConfirm
        objectName: "presetConfirmDialog"
        visible: false; z: 20; anchors.centerIn: parent; width: 400; radius: 6
        color: Theme.panelHi; border.color: Theme.warn
        height: Math.min(root.height - 40, presetConfirmCol.height + 30)
        property string file: ""
        property var info: ({})
        readonly property var changes: info.changes || []
        readonly property var notAllowed: info.notAllowed || []
        readonly property bool anyWarn: { for (var i = 0; i < changes.length; ++i) if (changes[i].warn) return true; return false; }
        function ask(f) { file = f; info = presets.preview(f); visible = true; }
        Flickable { anchors.fill: parent; anchors.margins: 15; clip: true; contentHeight: presetConfirmCol.height
        Column { id: presetConfirmCol; width: 370; spacing: 6
            Text { width: 370; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 12; font.bold: true
                   text: presetConfirm.info.ok ? "Load preset \"" + presetConfirm.info.name + "\"?" : "Cannot load this preset" }
            Text { width: 370; wrapMode: Text.WordWrap; color: Theme.warn; font.pixelSize: 11
                   visible: !presetConfirm.info.ok; text: presetConfirm.info.error || "" }
            Text { width: 370; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 11
                   visible: presetConfirm.info.ok === true && presetConfirm.changes.length === 0
                   text: "The device already has these settings" + (presetConfirm.notAllowed.length ? " (apart from the ones below)." : ".") }
            Repeater {
                model: presetConfirm.changes
                Text { width: 370; wrapMode: Text.WordWrap; font.pixelSize: 11
                       color: modelData.warn ? Theme.warn : Theme.text
                       text: (modelData.warn ? "⚠ " : "• ") + modelData.label + ": " + modelData.from + " → " + modelData.to }
            }
            Text { width: 370; wrapMode: Text.WordWrap; color: Theme.warn; font.pixelSize: 10; visible: presetConfirm.anyWarn
                   text: "⚠ can get louder, feed back or switch 48 V: turn your monitors down and take headphones off first. "
                       + "Changes are sent in a safe order (mutes first, unmutes last); level increases go in small, verified steps." }
            Text { width: 370; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 10
                   visible: presetConfirm.notAllowed.length > 0
                   text: "Skipped (not supported by OpenWitty): " + presetConfirm.notAllowed.join("; ") }
            Text { width: 370; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 10
                   visible: (presetConfirm.info.ignored || []).length > 0
                   text: "Not understood, ignored: " + (presetConfirm.info.ignored || []).join(", ") }
            Row { spacing: 8; anchors.horizontalCenter: parent.horizontalCenter
                ToggleButton { width: 70; text: "Cancel"; onClicked: presetConfirm.visible = false }
                ToggleButton { objectName: "presetApplyButton"; width: 70; text: "Apply"; active: true
                               activeColor: presetConfirm.anyWarn ? Theme.warn : Theme.accent
                               enabled: presetConfirm.info.ok === true && presetConfirm.changes.length > 0
                               onClicked: { presetConfirm.visible = false; presetsPage.status = "";
                                            presets.apply(presetConfirm.file); } } }
        } }
    }
}
