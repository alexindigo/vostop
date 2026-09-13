import QtQuick

//? Polyline history graph over `samples` (0..maxValue), fixed 120-sample window
Item {
    id: root

    property list<double> samples: []
    property double maxValue: 100.0
    property color lineColor: "#4fc3f7"
    property color gridColor: Qt.rgba(1, 1, 1, 0.07)
    property int gridDivisions: 4
    property int verticalDivisions: 0 //? 0 = off (existing users unaffected)
    property list<var> series: [] //? optional multi-series: one samples-list per line (overrides samples)
    property list<color> seriesColors: [] //? parallel to series; empty → all lineColor

    onSamplesChanged: canvas.requestPaint()
    onMaxValueChanged: canvas.requestPaint()
    onLineColorChanged: canvas.requestPaint()
    onSeriesChanged: canvas.requestPaint()
    onSeriesColorsChanged: canvas.requestPaint()
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
            for (let gv = 1; gv < root.verticalDivisions; ++gv) {
                const gx = Math.round(width * gv / root.verticalDivisions) + 0.5
                ctx.moveTo(gx, 0)
                ctx.lineTo(gx, height)
            }
            ctx.stroke()

            const drawLine = (data, color) => {
                const n = data.length
                if (n < 2)
                    return
                const dx = width / (n - 1)
                const scaleY = (v) => height - (Math.min(Math.max(v, 0), root.maxValue) / root.maxValue) * height
                ctx.beginPath()
                for (let i = 0; i < n; ++i) {
                    const x = i * dx
                    const y = scaleY(data[i])
                    if (i === 0) ctx.moveTo(x, y)
                    else ctx.lineTo(x, y)
                }
                ctx.strokeStyle = color
                ctx.lineWidth = 1.6
                ctx.stroke()
            }

            //? Multi-series: one line per entry, uniform alpha (caller colors)
            if (root.series.length > 0) {
                for (let s = 0; s < root.series.length; ++s)
                    drawLine(root.series[s], root.seriesColors.length > s ? root.seriesColors[s] : root.lineColor)
                ctx.restore()
                return
            }

            const data = root.samples
            if (data.length < 2) {
                ctx.restore()
                return
            }

            const n = data.length
            const dx = width / (n - 1)
            const scaleY = (v) => height - (Math.min(Math.max(v, 0), root.maxValue) / root.maxValue) * height

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