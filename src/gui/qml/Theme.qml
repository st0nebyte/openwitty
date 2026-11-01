pragma Singleton
import QtQuick

// Independent palette measured from public screenshots of Control Center 5.x
// (see docs/GUI_DESIGN.md "Colour measurements"). No proprietary artwork is used.
QtObject {
    readonly property color header: "#1e1d1e"
    readonly property color inputPanel: "#3e3e3e"
    readonly property color darkPanel: "#1f1d1e"
    readonly property color sectionBar: "#1f1d1e"
    readonly property color footer: "#1e1d1e"
    readonly property color bg: "#1e1d1e"
    readonly property color panel: "#262626"       // dialogs
    readonly property color panelHi: "#323232"
    readonly property color well: "#323031"        // meter background
    readonly property color line: "#0f0f0f"
    readonly property color button: "#4a4a4a"
    readonly property color buttonBorder: "#2a2a2a"
    readonly property color text: "#ececec"
    readonly property color textDim: "#a0a0a0"
    readonly property color textFaint: "#7a7a7a"
    readonly property color accent: "#8fc21d"      // active outline / text, "CONNECT 2"
    readonly property color accentFill: "#98bb0c"  // primary buttons (Next, Check for updates)
    readonly property color accentDim: "#5b6e15"
    readonly property color selected: "#75b9ff"     // icon of the channel the device's touch wheel controls
    readonly property color meterGreen: "#83b12a"
    readonly property color meterAmber: "#f8aa37"
    readonly property color danger: "#f01a1a"      // active MUTE
    readonly property color ok: "#83b12a"
    readonly property color warn: "#f8aa37"
    readonly property color bad: "#f01a1a"
    readonly property color sim: "#e0b040"
    readonly property int radius: 3
}
