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
            model: [{k: "audio", t: "Audio"}, {k: "leds", t: "LEDs"}, {k: "button", t: "Custom button"}]
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
        x: 16; y: 150; width: 330; spacing: 6
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
        x: 16; y: 150; width: 330; spacing: 10
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
        x: 16; y: 150; width: 330; spacing: 8
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

    // right column
    Column {
        x: 390; y: 60; width: 310; spacing: 10
        Text { color: Theme.text; font.pixelSize: 12; text: "OpenWitty 0.1.0 — unofficial, experimental; not affiliated with LEWITT" }
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
