import QtQuick
import QtQuick.Window

Window {
    id: win
    width: 1000; height: 560; visible: true
    title: "LEWITT CONNECT 2 — Control Center for Linux (unofficial)"
    color: Theme.bg
    minimumWidth: 760; minimumHeight: 440

    readonly property bool live: device.connection === "Connected"
    readonly property bool interactive: live && device.writable
    property bool showSettings: false
    property bool showWizard: false

    // Design space 760 x 420 (matches the proportions of the official window);
    // scaled uniformly to the window so layout stays faithful at any size.
    Item {
        id: stage
        width: 760; height: 430
        scale: Math.min(win.width / width, (win.height - banner.height) / height)
        transformOrigin: Item.TopLeft
        x: (win.width - width * scale) / 2
        y: banner.height

        // header
        Rectangle { id: header; width: parent.width; height: 44; color: Theme.panel
            Text { x: 14; anchors.verticalCenter: parent.verticalCenter; color: Theme.text; font.pixelSize: 15
                   text: "CONTROL CENTER" }
            Text { x: 152; anchors.verticalCenter: parent.verticalCenter; color: Theme.accent; font.pixelSize: 15
                   text: "| CONNECT 2" }
            DeviceStatusIndicator { x: 268; anchors.verticalCenter: parent.verticalCenter
                state: device.connection; detail: device.simulated ? "simulated" : device.backend }
            Row { anchors.right: parent.right; anchors.rightMargin: 14; anchors.verticalCenter: parent.verticalCenter
                  spacing: 8
                Text { anchors.verticalCenter: parent.verticalCenter; color: Theme.textDim; font.pixelSize: 11
                       text: "Sample rate:" }
                Repeater {
                    model: [44100, 48000, 96000]
                    ToggleButton { width: 54; text: (modelData / 1000) + " kHz"
                                   enabled: device.simulated   // real rate comes from the audio stack
                                   active: (device.values["sampleRate"] || 0) === modelData
                                   tip: device.simulated ? "Simulated sample rate" : "Set by the audio system (not controlled here)"
                                   onClicked: device.setSampleRate(modelData) }
                }
                ToggleButton { width: 28; text: "⚙"; tip: "Settings"; onClicked: win.showSettings = true }
            }
        }

        // column headings + dividers
        Row { y: header.height + 4
            Text { width: 360; horizontalAlignment: Text.AlignHCenter; color: Theme.textDim; font.pixelSize: 11; text: "Inputs" }
            Text { width: 230; horizontalAlignment: Text.AlignHCenter; color: Theme.textDim; font.pixelSize: 11; text: "Outputs" }
            Text { width: 170; horizontalAlignment: Text.AlignHCenter; color: Theme.textDim; font.pixelSize: 11; text: "Monitoring" }
        }
        Rectangle { x: 360; y: header.height; width: 1; height: parent.height - header.height; color: Theme.line }
        Rectangle { x: 590; y: header.height; width: 1; height: parent.height - header.height; color: Theme.line }
        ToggleButton { x: 8; y: header.height + 4; width: 70; text: "Autosetup"; enabled: win.interactive
                       tip: "Guided setup of the input channels"; onClicked: win.showWizard = true }

        Row { x: 6; y: header.height + 34; spacing: 6
            InputStrip { index: 1; interactive: win.interactive }
            InputStrip { index: 2; interactive: win.interactive }
        }
        Row { x: 368; y: header.height + 34; spacing: 8
            OutputStrip { group: "loop"; title: "Loopback"; interactive: win.interactive }
            OutputStrip { group: "spk"; title: "Loudspeaker"; interactive: win.interactive }
            OutputStrip { group: "hp"; title: "Headphone"; interactive: win.interactive }
        }
        MonitorSection { x: 596; y: header.height + 34; width: 160; interactive: win.interactive }

        // overlays
        Rectangle { visible: !win.live; anchors.fill: parent; anchors.topMargin: header.height; color: "#1f1f1f"; z: 5
            Text { anchors.centerIn: parent; color: Theme.text; font.pixelSize: 20; font.bold: true
                   text: device.connection === "Connecting" ? "Connecting…" : "No LEWITT device connected!" }
            Text { anchors.horizontalCenter: parent.horizontalCenter; y: parent.height / 2 + 24; color: Theme.textDim
                   font.pixelSize: 11; text: device.lastError.length ? device.lastError
                                         : "Use the buttons below to start the simulated device." }
        }
        SettingsView { visible: win.showSettings; anchors.fill: parent; z: 10; interactive: win.interactive
                       onCloseRequested: win.showSettings = false }
        AutosetupWizard { visible: win.showWizard; anchors.fill: parent; z: 11; interactive: win.interactive
                          onCloseRequested: win.showWizard = false }
    }

    // banner + dev controls (outside the design space)
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
