import QtQuick

// State button (Low-cut, 48V, Denoise, Dim, ...). inactive = grey; active = lime
// outline + lime text/icon; `danger` buttons (MUTE) turn solid red with white text.
Rectangle {
    id: root
    property alias text: label.text
    property string icon: ""            // GlyphIcon kind (optional)
    property bool active: false
    property bool danger: false
    property color activeColor: Theme.accent
    property string tip: ""
    property bool unavailable: false
    property int fontSize: 10
    property bool flat: false            // icon-only, no box (autogain "A" icon)
    property bool primary: false         // lime filled call-to-action (Next, Check for updates)
    signal clicked()
    implicitWidth: 60; implicitHeight: 20
    radius: Theme.radius
    color: flat ? "transparent" : primary ? Theme.accentFill
         : (active && danger) ? Theme.danger
         : active ? "#1f1f1f"
         : (root.unavailable ? Theme.button : mouse.pressed ? "#5a5a5a" : (mouse.containsMouse ? "#525252" : Theme.button))
    border.color: flat ? "transparent" : primary ? Theme.accentFill : (active && danger) ? Theme.danger
                : active ? activeColor : Theme.buttonBorder
    border.width: active && !danger ? 2 : 1
    opacity: (enabled && !unavailable) ? 1 : 0.45
    Text { id: label; anchors.centerIn: parent; font.pixelSize: root.fontSize
           color: root.primary ? "#101010" : (root.active && root.danger) ? "#fff"
                 : root.active ? root.activeColor : "#ffffff"
           font.bold: root.primary; visible: root.icon === "" }
    GlyphIcon { visible: root.icon !== ""; anchors.centerIn: parent
                width: Math.min(parent.height - 6, 18); height: width
                kind: root.icon; color: root.flat ? Theme.accent : (root.active ? root.activeColor : "#d0d0d0") }
    // An unavailable button still shows its tip on hover (why it is unavailable); only the click is blocked.
    MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true
                enabled: root.enabled
                onClicked: if (!root.unavailable) root.clicked()
                onEntered: if (root.tip.length) Hint.show(root.tip)
                onExited: Hint.clear() }
}
