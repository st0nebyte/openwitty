pragma Singleton
import QtQuick

// Text shown in the footer status bar (the original shows control hints there
// instead of floating tooltips) and the "Show tooltips" preference.
QtObject {
    property string text: ""
    property bool enabled: true
    function show(t) { if (enabled) text = t; }
    function clear() { text = ""; }
}
