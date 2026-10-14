import QtQuick

Rectangle {
    id: root
    property bool interactive: false
    function v(k, d) { var x = device.values["out." + k]; return x === undefined ? d : x; }
    color: Theme.panel; radius: Theme.radius; border.color: Theme.line
    implicitWidth: 330; implicitHeight: 380
    Column {
        anchors.fill: parent; anchors.margins: Theme.pad; spacing: 12
        Text { text: "OUTPUT"; color: Theme.text; font.pixelSize: 15; font.bold: true }
        Row { spacing: 14
            GainKnob { label: "VOLUME A"; from: -60; to: 0; step: 0.5; enabled: root.interactive
                       value: root.v("volumeA", 0)
                       onMoved: (nv) => device.setValue("out.volumeA", nv) }
            GainKnob { label: "VOLUME B"; from: -60; to: 0; step: 0.5; enabled: root.interactive
                       value: root.v("volumeB", 0)
                       onMoved: (nv) => device.setValue("out.volumeB", nv) }
        }
        Row { spacing: 16
            ToggleSwitch { enabled: root.interactive; label: "Dim"; checked: root.v("dim", false)
                           onToggled: (b) => device.setValue("out.dim", b) }
            ToggleSwitch { enabled: root.interactive; label: "Mono"; checked: root.v("mono", false)
                           onToggled: (b) => device.setValue("out.mono", b) }
        }
        Text { color: Theme.textDim; font.pixelSize: 11; wrapMode: Text.WordWrap; width: parent.width
               text: "Output grouping (which volume drives headphones vs. speakers) is not yet established from the official app; labels are provisional." }
    }
}
