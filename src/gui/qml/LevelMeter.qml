import QtQuick

Rectangle {
    id: root
    property real level: -60      // dB, -60..0
    property real minDb: -60
    implicitWidth: 14; implicitHeight: 120
    radius: 3; color: Theme.panelHi
    readonly property real frac: Math.max(0, Math.min(1, (level - minDb) / -minDb))
    Rectangle {
        width: parent.width; height: parent.height * root.frac
        anchors.bottom: parent.bottom; radius: 3
        color: root.level > -6 ? Theme.bad : root.level > -18 ? Theme.warn : Theme.ok
    }
}
