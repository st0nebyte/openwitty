import QtQuick

// Loopback (meter + mute), Loudspeaker / Headphone (meter + fader + mute).
Item {
    id: root
    property string group: "spk"       // loop | spk | hp
    property string title: ""
    property bool interactive: false
    function v(k, d) { var x = device.values[root.group + "." + k]; return x === undefined ? d : x; }
    readonly property bool hasFader: group !== "loop"
    implicitWidth: hasFader ? 70 : 66; implicitHeight: 290

    Text { anchors.horizontalCenter: parent.horizontalCenter; y: 0; color: Theme.text; font.pixelSize: 12; text: root.title }
    Row {
        y: 30; spacing: 4; anchors.horizontalCenter: parent.horizontalCenter
        LevelMeter { stereo: true; scale: !root.hasFader; height: 200
                     levelL: root.v("levelL", -60); levelR: root.v("levelR", -60) }
        Fader { visible: root.hasFader; height: 200; enabled: root.interactive
                value: root.v("volume", 0)
                onMoved: (nv) => device.setValue(root.group + ".volume", nv) }
    }
    ToggleButton { anchors.horizontalCenter: parent.horizontalCenter; y: 258; width: 62; text: "MUTE"
                   activeColor: Theme.bad; enabled: root.interactive; active: root.v("mute", false)
                   onClicked: device.setValue(root.group + ".mute", !active) }
}
