import QtQuick

//? Polyline history graph over `samples` (0..maxValue), fixed 120-sample window
Item {
    id: root

    property list<double> samples: []
    property double maxValue: 100.0
    property color lineColor: "#4fc3f7"
    property color fillColor: Qt.rgba(lineColor.r, lineColor.g, lineColor.b, 0.18)
    property color gridColor: Qt.rgba(1, 1, 1, 0.07)
    property int gridDivisions: 4

    onSamplesChanged: canvas.requestPaint()
    onMaxValueChanged: canvas.requestPaint()
    onLineColorChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        anchors.fill: parent
        antialiasing: true

        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.save()

            //? Grid
            ctx.strokeStyle = root.gridColor
            ctx.lineWidth = 1
            ctx.beginPath()
            for (let g = 1; g < root.gridDivisions; ++g) {
                const gy = Math.round(height * g / root.gridDivisions) + 0.5
                ctx.moveTo(0, gy)
                ctx.lineTo(width, gy)
            }
            ctx.stroke()

            const data = root.samples
            if (data.length < 2) {
                ctx.restore()
                return
            }

            const n = data.length
            const dx = width / (n - 1)
            const scaleY = (v) => height - (Math.min(Math.max(v, 0), root.maxValue) / root.maxValue) * height

            //? Fill under curve
            ctx.beginPath()
            ctx.moveTo(0, height)
            for (let i = 0; i < n; ++i)
                ctx.lineTo(i * dx, scaleY(data[i]))
            ctx.lineTo(width, height)
            ctx.closePath()
            ctx.fillStyle = root.fillColor
            ctx.fill()

            //? Line
            ctx.beginPath()
            for (let i2 = 0; i2 < n; ++i2) {
                const x = i2 * dx
                const y = scaleY(data[i2])
                if (i2 === 0) ctx.moveTo(x, y)
                else ctx.lineTo(x, y)
            }
            ctx.strokeStyle = root.lineColor
            ctx.lineWidth = 1.6
            ctx.stroke()

            ctx.restore()
        }
    }
}