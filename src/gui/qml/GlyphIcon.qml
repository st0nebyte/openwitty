import QtQuick

// Shared 24-unit Lucide outline set, plus the original low-cut curve.
// Oversample the vector source before the parent design-stage transform so
// 100–200% desktop scaling does not enlarge a tiny rasterized Canvas texture.
Item {
    id: root
    property string kind: "mic"
    property color color: "#a8a8a8"
    // the selection-colour variant (channel icons only: mic, guitar, loop, speakers, headphones)
    property bool highlighted: false
    implicitWidth: 24
    implicitHeight: 24
    readonly property string fileName: ({mic: "mic", guitar: "guitar", loop: "repeat",
        speakers: "monitor-speaker", headphones: "headphones", shuffle: "shuffle",
        lowcut: "lowcut", clipguard: "shield-check", laptop: "laptop",
        settings: "settings"})[kind] || ""

    Image {
        id: icon
        objectName: "glyphImage"
        anchors.fill: parent
        source: root.fileName ? "qrc:/icons/" + root.fileName
                                + (root.highlighted && ["mic", "guitar", "loop", "speakers", "headphones"].indexOf(root.kind) >= 0
                                   ? "-selected" : "") + ".svg" : ""
        sourceSize.width: Math.ceil(root.width * 4)
        sourceSize.height: Math.ceil(root.height * 4)
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        asynchronous: false
        // No shader tint: software scene-graph backends do not render shader
        // effects, which would make every icon disappear. State borders/labels
        // carry the accent color; the SVG strokes stay consistently light.
    }
}
