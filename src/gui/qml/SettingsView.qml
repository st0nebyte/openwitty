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
    function v(k, d) { var x = device.values[k]; return x === undefined ? d : x; }
    property string open: "audio"

    Text { x: 16; y: 12; text: "Settings"; color: Theme.text; font.pixelSize: 15 }
    ToggleButton { anchors.right: parent.right; anchors.rightMargin: 16; y: 10; width: 60; text: "Close"
                   onClicked: root.closeRequested() }
    Rectangle { x: 0; y: 44; width: parent.width; height: 1; color: Theme.line }

    Column {
        x: 16; y: 60; width: 330; spacing: 6
        Repeater {
            model: [{k: "audio", t: "Audio"}, {k: "leds", t: "LEDs"}, {k: "button", t: "Custom button"},
                    {k: "desktop", t: "Desktop integration"}]
            Rectangle {
                width: 330; height: 26; color: Theme.panel; radius: 2
                Text { x: 8; anchors.verticalCenter: parent.verticalCenter; color: Theme.text; font.pixelSize: 12; text: modelData.t }
                Text { anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
                       color: Theme.textDim; text: root.open === modelData.k ? "▾" : "◂" }
                MouseArea { anchors.fill: parent; onClicked: root.open = modelData.k }
            }
        }
    }

    // Section bodies
    Column {
        visible: root.open === "audio"
        x: 16; y: 190; width: 330; spacing: 6
        ToggleSwitch { width: 330; label: "Gain lock (block external gain changes)"; enabled: root.interactive
                       checked: root.v("set.gainLock", false)
                       onToggled: (b) => device.setValue("set.gainLock", b) }
        ToggleSwitch { width: 330; label: "Input/playback mix applies to speakers"; enabled: root.interactive
                       checked: root.v("set.mixToSpeakers", false)
                       onToggled: (b) => device.setValue("set.mixToSpeakers", b) }
        ToggleSwitch { width: 330; label: "High impedance headphone mode"; enabled: root.interactive
                       checked: root.v("set.highImpedance", false)
                       onToggled: (b) => device.setValue("set.highImpedance", b) }
        ToggleSwitch { width: 330; label: "High volume speaker mode"; enabled: root.interactive
                       checked: root.v("set.highVolume", false)
                       onToggled: (b) => confirmLoud.ask(b) }
        ToggleSwitch { width: 330; label: "Mobile streaming mode (routing change)"; enabled: false
                       checked: root.v("set.mobileStreaming", false) }
        Text { width: 330; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 10
               text: "Streaming modes change USB channel routing and (macOS mode) restart the device. They are not enabled in this build." }
        ToggleSwitch { width: 330; label: "macOS streaming mode"; enabled: device.simulated && root.interactive
                       checked: root.v("set.macosStreaming", false)
                       tip: device.simulated ? "Simulated: restarts the (simulated) device" : "Not enabled on real hardware"
                       onToggled: (b) => confirmMode.ask(b) }
    }
    Column {
        visible: root.open === "leds"
        x: 16; y: 190; width: 330; spacing: 10
        Text { color: Theme.text; font.pixelSize: 12; text: "Brightness: " + root.v("set.ledBrightness", 0) + " %" }
        Fader_h { width: 330; enabled: root.interactive; value: root.v("set.ledBrightness", 0)
                  onMoved: (nv) => device.setValue("set.ledBrightness", nv) }
        Repeater {
            model: [{k: "icons", t: "Icons"}, {k: "mute", t: "Mute"}, {k: "parameter", t: "Parameter"},
                    {k: "metering", t: "Metering"}, {k: "gainReduction", t: "Gain reduction"}]
            Row {
                id: ledRow
                property var elem: modelData
                spacing: 6
                Text { width: 100; color: Theme.text; font.pixelSize: 11; text: ledRow.elem.t
                       anchors.verticalCenter: parent.verticalCenter }
                Repeater {
                    model: ["#ff0000", "#ff8800", "#ffee00", "#00cc44", "#0099ff", "#8844ff", "#ffffff"]
                    Rectangle {
                        id: sw
                        property string key: "set.led." + ledRow.elem.k
                        width: 22; height: 18; radius: 3; color: modelData
                        opacity: root.interactive ? 1 : 0.4
                        property bool sel: root.v(sw.key, "") === modelData
                        border.width: sel ? 2 : 1
                        border.color: sel ? Theme.text : Theme.line
                        MouseArea { anchors.fill: parent; enabled: root.interactive
                                    onClicked: device.setValue(sw.key, sw.color.toString()) }
                    }
                }
            }
        }
    }
    Column {
        visible: root.open === "button"
        x: 16; y: 190; width: 330; spacing: 8
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
                opacity: root.interactive ? 1 : 0.55
                Rectangle { id: dot; width: 14; height: 14; radius: 7; color: "transparent"
                            border.color: Theme.textDim; anchors.verticalCenter: parent.verticalCenter
                            Rectangle { anchors.centerIn: parent; width: 8; height: 8; radius: 4; color: Theme.accent
                                        visible: root.v("set.customButton", -1) === opt.o.v } }
                Text { x: 24; anchors.verticalCenter: parent.verticalCenter; color: Theme.text
                       font.pixelSize: 11; text: opt.o.t }
                MouseArea { anchors.fill: parent; enabled: root.interactive
                            onClicked: device.setValue("set.customButton", opt.o.v) }
            }
        }
        Text { width: 330; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 10
               text: root.v("set.customButtonKnown", true) ? "Stored value: " + root.v("set.customButton", "?")
                     : "Stored value " + root.v("set.customButton", "?") + " is not offered by the CONNECT 2 UI." }
    }

    // Desktop integration: desktop (KDE/PipeWire) volume sync, background service, vendor fallback,
    // direct output control, kernel compatibility quirk status. Device-writing options need a dialog.
    Flickable {
        id: desktopPage
        objectName: "desktopIntegrationPage"
        visible: root.open === "desktop"
        x: 16; y: 190; width: 340; height: root.height - 200; clip: true
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
            Text { width: 310; color: Theme.text; font.pixelSize: 12; text: "Options that write the device" }
            ToggleSwitch { width: 310; label: "Vendor fallback (writes the device)"
                           enabled: integration.desktopSync
                           checked: integration.vendorFallback
                           tip: "Only for a device that does not respond to the desktop volume."
                           onToggled: (b) => { if (b) confirmFallback.visible = true; else integration.setVendorFallback(false, false); } }
            Text { width: 310; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 10
                   text: "Only if the desktop reports that the CONNECT 2 did not follow its volume: the background "
                       + "service then applies it through the device's own protocol." }

            Rectangle { width: 310; height: 1; color: "#333" }
            ToggleSwitch { width: 310; label: "Speaker/headphone control (writes the device)"
                           checked: integration.outputControl
                           tip: "Lets this window change the speaker and headphone volume and mute directly."
                           onToggled: (b) => { if (b) confirmOutput.visible = true; else integration.setOutputControl(false, false); } }
            Text { width: 310; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 10
                   text: "OpenWitty's own speaker and headphone faders (separate from desktop sync)." }

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

    // right column
    Column {
        visible: root.open !== "desktop"
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
        ToggleSwitch { width: 310; label: "Show tooltips"; checked: Hint.enabled
                       onToggled: (b) => { Hint.enabled = b; if (!b) Hint.clear(); } }
        ToggleSwitch { width: 310; label: "Retrigger onboarding"; checked: false
                       onToggled: (b) => { if (b) root.onboardingRequested(); } }
    }
    ToggleButton { anchors.right: parent.right; anchors.rightMargin: 16; anchors.bottom: parent.bottom
                   visible: root.open !== "desktop"
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
        id: confirmOutput
        visible: false; z: 20; anchors.centerIn: parent; width: 320; height: 150; radius: 6
        color: Theme.panelHi; border.color: Theme.warn
        Column { anchors.centerIn: parent; spacing: 10
            Text { width: 290; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 11
                   text: "OpenWitty will be able to change the CONNECT 2's speaker and headphone volume and mute. "
                       + "Only those settings; each change is limited to 10 dB louder per step and read back. "
                       + "Lower your monitors and headphones first." }
            Row { spacing: 8; anchors.horizontalCenter: parent.horizontalCenter
                ToggleButton { width: 70; text: "Cancel"; onClicked: confirmOutput.visible = false }
                ToggleButton { width: 70; text: "Enable"; active: true; activeColor: Theme.warn
                               onClicked: { confirmOutput.visible = false; integration.setOutputControl(true, true); } } }
        }
    }

    Rectangle {
        id: confirmReset
        visible: false; z: 20; anchors.centerIn: parent; width: 300; height: 140; radius: 6
        color: Theme.panelHi; border.color: Theme.warn
        Column { anchors.centerIn: parent; spacing: 10
            Text { width: 270; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 11
                   text: "Turn off desktop volume sync, the vendor fallback, speaker/headphone control, the background "
                       + "service and the system tray icon? The kernel quirk, if you installed it, is not touched." }
            Row { spacing: 8; anchors.horizontalCenter: parent.horizontalCenter
                ToggleButton { width: 70; text: "Cancel"; onClicked: confirmReset.visible = false }
                ToggleButton { width: 70; text: "Reset"; primary: true
                               onClicked: { confirmReset.visible = false; integration.resetToDefaults(); } } }
        }
    }

    Rectangle {
        id: confirmLoud
        visible: false; z: 20; anchors.centerIn: parent; width: 260; height: 110; radius: 6
        color: Theme.panelHi; border.color: Theme.warn
        function ask(on) { if (!on) device.setValue("set.highVolume", false); else visible = true; }
        Column { anchors.centerIn: parent; spacing: 10
            Text { width: 230; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 11
                   text: "High volume speaker mode raises the maximum speaker output level. Lower your monitors first." }
            Row { spacing: 8; anchors.horizontalCenter: parent.horizontalCenter
                ToggleButton { width: 70; text: "Cancel"; onClicked: confirmLoud.visible = false }
                ToggleButton { width: 70; text: "Enable"; active: true; activeColor: Theme.warn
                               onClicked: { confirmLoud.visible = false; device.setValue("set.highVolume", true); } } }
        }
    }
}
