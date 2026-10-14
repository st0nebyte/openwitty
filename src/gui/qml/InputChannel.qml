import QtQuick

Rectangle {
    id: root
    property int index: 1                // 1 or 2
    property bool hasPhantom: index === 1
    property bool interactive: false
    readonly property string p: "in" + index + "."
    function v(k, d) { var x = device.values[p + k]; return x === undefined ? d : x; }
    color: Theme.panel; radius: Theme.radius; border.color: Theme.line
    implicitWidth: 330; implicitHeight: 380

    Column {
        anchors.fill: parent; anchors.margins: Theme.pad; spacing: 12
        Text { text: "INPUT " + root.index; color: Theme.text; font.pixelSize: 15; font.bold: true }
        Row {
            spacing: 14
            GainKnob {
                enabled: root.interactive
                value: root.v("gain", 0)
                onMoved: (nv) => device.setValue(root.p + "gain", nv)
            }
            LevelMeter { level: root.v("level", -60) }
        }
        Row {
            spacing: 6
            Repeater {
                model: ["Clean", "Warm", "Vivid"]
                FeatureButton {
                    enabled: root.interactive
                    text: modelData
                    active: root.v("preamp", 0) === index
                    onClicked: device.setValue(root.p + "preamp", index)
                }
            }
        }
        Grid {
            columns: 2; spacing: 6; columnSpacing: 16
            ToggleSwitch { enabled: root.interactive; label: "High-pass"; checked: root.v("highPass", false)
                           onToggled: (b) => device.setValue(root.p + "highPass", b) }
            ToggleSwitch { enabled: root.interactive; label: "Clipguard"; checked: root.v("clipguard", false)
                           onToggled: (b) => device.setValue(root.p + "clipguard", b) }
            ToggleSwitch { enabled: root.interactive; label: "Denoise"; checked: root.v("denoise", false)
                           onToggled: (b) => device.setValue(root.p + "denoise", b) }
            ToggleSwitch { enabled: root.interactive; label: "Compress"; checked: root.v("compress", false)
                           onToggled: (b) => device.setValue(root.p + "compress", b) }
            ToggleSwitch { enabled: root.interactive; label: "Mute"; checked: root.v("mute", false)
                           onToggled: (b) => device.setValue(root.p + "mute", b) }
            ToggleSwitch { visible: root.hasPhantom; enabled: root.interactive; label: "48 V phantom"
                           checked: root.v("phantom", false)
                           onToggled: (b) => phantomConfirm.ask(b) }
        }
        Row {
            spacing: 6
            FeatureButton { enabled: root.interactive; text: "Auto gain"; width: 100
                            onClicked: device.trigger("autogain" + root.index) }
        }
    }

    // Phantom power is safety-relevant: always confirm before enabling.
    Rectangle {
        id: phantomConfirm
        property bool pending: false
        function ask(b) { pending = b; if (!b) { device.setValue(root.p + "phantom", false); return; } visible = true; }
        visible: false; anchors.centerIn: parent; width: 280; height: 130; radius: 8
        color: Theme.panelHi; border.color: Theme.warn; z: 10
        Column { anchors.centerIn: parent; spacing: 12
            Text { text: "Enable 48 V phantom power?\nDisconnect ribbon/dynamic mics first."
                   color: Theme.text; horizontalAlignment: Text.AlignHCenter }
            Row { spacing: 10; anchors.horizontalCenter: parent.horizontalCenter
                FeatureButton { text: "Cancel"; onClicked: phantomConfirm.visible = false }
                FeatureButton { text: "Enable"; active: true
                    onClicked: { phantomConfirm.visible = false; device.setValue(root.p + "phantom", true); } }
            }
        }
    }
}
