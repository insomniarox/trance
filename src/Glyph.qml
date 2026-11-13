import QtQuick
import QtQuick.Shapes

// A small line icon, drawn rather than taken from a font, so it is the same
// on every machine and takes any color. The shapes are on a 24-unit grid.
Item {
    id: glyph
    property string name
    property color color: Ui.text
    property real size: 16
    implicitWidth: size
    implicitHeight: size

    readonly property var shapes: ({
        "close": "M6 6 18 18 M18 6 6 18",
        "check": "M20 6 9 17 4 12",
        "plus": "M12 5v14 M5 12h14",
        "chevron-down": "M6 9l6 6 6-6",
        "chevron-right": "M9 6l6 6-6 6",
        "up": "M12 19V5 M5 12l7-7 7 7",
        "down": "M12 5v14 M19 12l-7 7-7-7",
        "copy": "M10 8h10a2 2 0 0 1 2 2v10a2 2 0 0 1-2 2H10a2 2 0 0 1-2-2V10a2 2 0 0 1 2-2z M4 16a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h10a2 2 0 0 1 2 2",
        "trash": "M3 6h18 M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6 M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2 M10 11v6 M14 11v6",
        "undo": "M9 14 4 9l5-5 M4 9h10.5a5.5 5.5 0 0 1 0 11H11",
        "redo": "M15 14l5-5-5-5 M20 9H9.5a5.5 5.5 0 0 0 0 11H13",
        "search": "M3 11a8 8 0 1 0 16 0a8 8 0 1 0-16 0 M21 21l-4.3-4.3",
        "list": "M8 6h13 M8 12h13 M8 18h13 M3 6h.5 M3 12h.5 M3 18h.5",
        "previous": "M19 20 9 12l10-8z M5 19V5",
        "next": "M5 4l10 8-10 8z M19 5v14",
        "shuffle": "M2 18h1.4c1.3 0 2.5-.6 3.3-1.7l6.1-8.6c.7-1.1 2-1.7 3.3-1.7H22 M18 2l4 4-4 4 M2 6h1.9c1.5 0 2.9.9 3.6 2.2 M22 18h-5.9c-1.3 0-2.6-.7-3.3-1.8l-.5-.8 M18 14l4 4-4 4",
        "edit": "M12 20h9 M16.5 3.5a2.12 2.12 0 0 1 3 3L7 19l-4 1 1-4z",
        "settings": "M21 4h-7 M10 4H3 M21 12h-9 M8 12H3 M21 20h-5 M12 20H3 M14 2v4 M8 10v4 M16 18v4",
        "help": "M2 12a10 10 0 1 0 20 0a10 10 0 1 0-20 0 M9.1 9a3 3 0 0 1 5.8 1c0 2-3 3-3 3 M12 17h.01",
        "fullscreen": "M8 3H5a2 2 0 0 0-2 2v3 M21 8V5a2 2 0 0 0-2-2h-3 M3 16v3a2 2 0 0 0 2 2h3 M16 21h3a2 2 0 0 0 2-2v-3",
        "windowed": "M8 3v3a2 2 0 0 1-2 2H3 M21 8h-3a2 2 0 0 1-2-2V3 M3 16h3a2 2 0 0 1 2 2v3 M16 21v-3a2 2 0 0 1 2-2h3"
    })

    Shape {
        anchors.centerIn: parent
        width: 24
        height: 24
        scale: glyph.size / 24
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: glyph.color
            strokeWidth: glyph.size < 18 ? 2.2 : 2
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: glyph.shapes[glyph.name] || "" }
        }
    }
}
