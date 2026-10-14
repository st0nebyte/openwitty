import QtQuick

// Loopback (meters + mute) / Loudspeaker / Headphone (meters + fader + mute).
Item {
    id: root
    property string group: "spk"       // loop | spk | hp
    property bool interactive: false
    function v(k, d) { var x = device.values[root.group + "." + k]; return x === undefined ? d : x; }
    readonly property bool hasFader: group !== "loop"
    readonly property bool pending: device.values["pending." + root.group + ".volume"] === true
                                    || device.values["pending." + root.group + ".mute"] === true
    width: 85; height: 267
    Rectangle { visible: root.pending; x: 74; y: 8; width: 6; height: 6; radius: 3; color: Theme.warn }

    GlyphIcon { x: 42.5 - width / 2; y: 8; width: 30; height: 30
                kind: group === "loop" ? "loop" : group === "spk" ? "speakers" : "headphones"
                color: "#f0f0f0" }
    LevelMeter { x: group === "loop" ? 7 : 5; y: 44; height: 187; stereo: true; scale: true
                 levelL: root.v("levelL", -60); levelR: root.v("levelR", -60) }
    Fader { visible: root.hasFader; x: 55; y: 44; height: 187; enabled: root.interactive
            tip: root.group === "spk" ? "Adjust the loudspeaker volume" : "Adjust the headphone volume"
            value: root.v("volume", 0)
            onMoved: (nv) => device.setValue(root.group + ".volume", nv) }
    ToggleButton { x: 18; y: 240; width: 50; height: 24; text: "MUTE"; danger: true; tip: "Mute the signal"
                   enabled: root.interactive; active: root.v("mute", false)
                   onClicked: device.setValue(root.group + ".mute", !active) }
}
