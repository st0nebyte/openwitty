import QtQuick

// Settings-page switch: lime track when on, dark outlined track when off.
Item {
    id: root
    property bool checked: false
    property string label: ""
    property string tip: ""
    signal toggled(bool value)
    implicitWidth: 330; implicitHeight: 26
    opacity: enabled ? 1 : 0.45
    Text { text: root.label; color: Theme.text; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
    Rectangle {
        width: 34; height: 16; radius: 8; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
        color: root.checked ? Theme.accentFill : "#1b1b1b"; border.color: root.checked ? Theme.accentFill : "#8a8a8a"
        Rectangle { width: 12; height: 12; radius: 6; y: 2; x: root.checked ? 20 : 2; color: "#f2f2f2"
                    Behavior on x { NumberAnimation { duration: 80 } } }
    }
    MouseArea { anchors.fill: parent; hoverEnabled: true; onClicked: root.toggled(!root.checked)
                onEntered: if (root.tip.length) Hint.show(root.tip); onExited: Hint.clear() }
}
