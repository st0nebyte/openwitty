import QtQuick
import QtQuick.Window

Window {
    id: win
    width: 1008; height: 566; visible: true
    title: "LEWITT CONNECT 2 — Control Center for Linux (unofficial)"
    color: Theme.footer
    minimumWidth: 720; minimumHeight: 440

    readonly property bool live: device.connection === "Connected"
    readonly property bool interactive: live && device.writable
    property bool showSettings: false
    property bool showWizard: false
    property bool showOnboarding: false
    readonly property bool hasPending: Object.keys(device.values).some(function (k) { return k.indexOf("pending.") === 0; })

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
            DeviceStatusIndicator { x: 262; anchors.verticalCenter: parent.verticalCenter
                state: device.connection; detail: device.simulated ? "simulated" : device.backend }
            Text { x: 461; anchors.verticalCenter: parent.verticalCenter; color: Theme.textDim; font.pixelSize: 11
                   text: "Sample rate:" }
            Rectangle {   // sample-rate dropdown (simulation only; real rate comes from the audio stack)
                id: rateBox
                x: 549; y: 14; width: 120; height: 26; radius: 3; color: "#515151"
                border.color: "#5a5a5a"
                opacity: device.simulated ? 1 : 0.55
                Text { x: 8; anchors.verticalCenter: parent.verticalCenter; color: "#f0f0f0"; font.pixelSize: 11
                       text: ((device.values["sampleRate"] || 0) / 1000).toString().replace(".", ".") + " kHz" }
                Text { anchors.right: parent.right; anchors.rightMargin: 7; anchors.verticalCenter: parent.verticalCenter
                       color: "#777"; font.pixelSize: 9; text: "▴\n▾"; lineHeight: 0.75 }
                MouseArea { anchors.fill: parent; enabled: device.simulated; onClicked: rateMenu.visible = !rateMenu.visible }
                ToolTip_ { visible: rateMa.containsMouse; text: device.simulated ? "Simulated sample rate"
                                                              : "Set by the audio system (not controlled here)" }
                MouseArea { id: rateMa; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
            }
            ToggleButton { x: 686; y: 16; width: 21; height: 22; icon: ""; text: "⚙"; fontSize: 13
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
        ToggleButton { x: 17; y: 55; width: 69; height: 19; text: "Autosetup"; enabled: win.interactive
                       tip: "Automatically find the perfect settings for your inputs"; onClicked: win.showWizard = true }
        // vertical dividers
        Rectangle { x: 155; y: 78; width: 1; height: 267; color: "#1b1b1b" }
        Rectangle { x: 310; y: 50; width: 1; height: 295; color: "#000" }
        Rectangle { x: 395; y: 78; width: 1; height: 267; color: "#1a1a1a" }
        Rectangle { x: 480; y: 78; width: 1; height: 267; color: "#1a1a1a" }
        Rectangle { x: 565; y: 50; width: 1; height: 295; color: "#000" }

        InputStrip { x: 3; y: 78; index: 1; interactive: win.interactive }
        InputStrip { x: 157; y: 78; index: 2; interactive: win.interactive }
        OutputStrip { x: 310; y: 78; group: "loop"; interactive: win.interactive }
        OutputStrip { x: 395; y: 78; group: "spk"; interactive: win.interactive }
        OutputStrip { x: 480; y: 78; group: "hp"; interactive: win.interactive }
        MonitorSection { x: 568; y: 78; interactive: win.interactive }

        // overlays
        Rectangle { visible: !win.live; x: 0; y: 50; width: parent.width; height: 323; color: "#1f1f1f"; z: 5
            Text { anchors.centerIn: parent; color: "#fff"; font.pixelSize: 20; font.bold: true
                   text: device.values["restarting"] ? "Device restarting… (simulated)"
                         : device.connection === "Connecting" ? "Connecting…" : "No LEWITT device connected!" }
            Text { anchors.horizontalCenter: parent.horizontalCenter; y: parent.height / 2 + 26; color: Theme.textDim
                   font.pixelSize: 11; text: device.lastError.length ? device.lastError
                                         : "Use the buttons above to start the simulated device." }
        }
        SettingsView { visible: win.showSettings; x: 0; y: 0; width: parent.width; height: parent.height; z: 10
                       interactive: win.interactive; onCloseRequested: win.showSettings = false
                       onOnboardingRequested: { win.showSettings = false; win.showOnboarding = true; } }
        OnboardingOverlay { visible: win.showOnboarding; x: 0; y: 0; width: parent.width; height: parent.height; z: 12
                            onFinished: win.showOnboarding = false }
        AutosetupWizard { visible: win.showWizard; x: 0; y: 0; width: parent.width; height: parent.height; z: 11
                          interactive: win.interactive; onCloseRequested: win.showWizard = false }
    }

    // simulation banner + development controls (outside the design space)
    Rectangle {
        id: banner
        width: parent.width; height: 28; color: device.simulated ? "#2e2813" : "#2b1d1d"
        Text { anchors.verticalCenter: parent.verticalCenter; x: 10; font.pixelSize: 11
               color: device.simulated ? Theme.sim : Theme.bad
               text: device.simulated
                     ? "SIMULATED DEVICE — no hardware is controlled; values shown are not device data."
                     : "Hardware writes are DISABLED: the control protocol is not verified on a real CONNECT 2." }
        Row { anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter; spacing: 6
            ToggleButton { width: 90; height: 20; text: "Mock device"; onClicked: device.connectMock() }
            ToggleButton { width: 100; height: 20; text: "USB (read-only)"; onClicked: device.connectUsb() }
            ToggleButton { width: 52; height: 20; text: "Unplug"; visible: device.simulated; onClicked: device.simulateUnplug() }
            ToggleButton { width: 52; height: 20; text: "Replug"; visible: device.simulated; onClicked: device.simulateReplug() }
        }
    }
}
