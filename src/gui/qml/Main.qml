import QtQuick
import QtQuick.Window

Window {
    id: win
    width: 1060; height: 560; visible: true
    title: "LEWITT CONNECT 2 — Linux Control Center (unofficial)"
    color: Theme.bg
    minimumWidth: 900; minimumHeight: 520

    readonly property bool live: device.connection === "Connected"
    readonly property bool interactive: live && device.writable

    Rectangle {  // header
        id: header
        height: 52; width: parent.width; color: Theme.panel
        Text { x: Theme.pad; anchors.verticalCenter: parent.verticalCenter
               text: "CONNECT 2"; color: Theme.text; font.pixelSize: 18; font.bold: true }
        DeviceStatusIndicator {
            anchors.centerIn: parent
            state: device.connection
            detail: device.backend + (device.simulated ? " (simulated)" : "")
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: Theme.pad
            anchors.verticalCenter: parent.verticalCenter; spacing: 8
            FeatureButton { text: "Mock device"; width: 100; onClicked: device.connectMock() }
            FeatureButton { text: "USB (read-only)"; width: 120; onClicked: device.connectUsb() }
            FeatureButton { text: "Unplug"; visible: device.simulated; onClicked: device.simulateUnplug() }
            FeatureButton { text: "Replug"; visible: device.simulated; onClicked: device.simulateReplug() }
        }
    }

    Rectangle {  // banner
        id: banner
        y: header.height; width: parent.width; height: 30
        color: device.simulated ? "#2a2a1a" : "#2b1d1d"
        Text { anchors.centerIn: parent; color: device.simulated ? Theme.warn : Theme.bad; font.pixelSize: 12
               text: device.simulated
                     ? "SIMULATED DEVICE — no hardware is being controlled. Controls are unverified against real hardware."
                     : "Hardware writes are DISABLED: the control protocol is not yet verified on a real CONNECT 2." }
    }

    Row {
        y: header.height + banner.height + 14; x: Theme.pad; spacing: 14
        InputChannel { index: 1; interactive: win.interactive }
        InputChannel { index: 2; interactive: win.interactive }
        OutputSection { interactive: win.interactive }
    }

    Text {
        anchors.bottom: parent.bottom; anchors.margins: 8; x: Theme.pad
        color: device.lastError.length ? Theme.bad : Theme.textDim; font.pixelSize: 12
        text: device.lastError.length ? "Last error: " + device.lastError : "Ready"
    }
}
