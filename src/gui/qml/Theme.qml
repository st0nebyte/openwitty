pragma Singleton
import QtQuick

// Independent palette inspired by the official guide screenshots (near-black
// panels, olive-green active state). No proprietary assets are used.
QtObject {
    readonly property color bg: "#1b1b1b"
    readonly property color panel: "#222222"
    readonly property color panelHi: "#2c2c2c"
    readonly property color well: "#161616"
    readonly property color line: "#3a3a3a"
    readonly property color text: "#e4e4e4"
    readonly property color textDim: "#8c8c8c"
    readonly property color accent: "#86b82e"     // active/selected
    readonly property color accentDim: "#4b6a1c"
    readonly property color ok: "#6fae3a"
    readonly property color warn: "#d7a73a"
    readonly property color bad: "#d9534a"
    readonly property color sim: "#e0b040"
    readonly property int radius: 4
}
