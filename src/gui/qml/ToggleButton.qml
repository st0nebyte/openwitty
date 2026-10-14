import QtQuick

// Small rectangular state button (Low-cut, 48V, Denoise, MUTE, Dim, ...).
Rectangle {
    id: root
    property alias text: label.text
    property bool active: false
    property color activeColor: Theme.accent
    property string tip: ""
    property bool unavailable: false     // feature restricted (e.g. DSP at 96 kHz)
    signal clicked()
    implicitWidth: 60; implicitHeight: 24
    radius: Theme.radius
    color: mouse.pressed ? Theme.panelHi : Theme.panel
    border.color: active ? activeColor : Theme.line
    border.width: active ? 1.5 : 1
    opacity: (enabled && !unavailable) ? 1 : 0.4
    Text { id: label; anchors.centerIn: parent; font.pixelSize: 11
           color: root.active ? root.activeColor : Theme.text }
    MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true
                enabled: root.enabled && !root.unavailable
                onClicked: root.clicked() }
    ToolTip_ { visible: mouse.containsMouse && root.tip.length > 0; text: root.tip }
}
