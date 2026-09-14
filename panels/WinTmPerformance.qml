pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vostop

//? XP Task Manager Performance tab — modernized: borderless rounded cards on
//? the theme's window background (no bevels), soft surfaces, LED-matrix
//? equalizer gauges, single-total history lines, Linux-standard
//? Memory/Swap metrics instead of Page File. All type and chrome metrics
//? derive from u = width/431 (1u = 1px in the reference screenshot), so the
//? reference's text-to-panel proportions hold at any size.
Item {
    id: root
    property string panelTitle: qsTr("Win-TM Performance")
    //? Content size propagates up: VostopPanel -> PanelLayout -> window minimum
    implicitWidth: flow.implicitWidth + root.px(20)
    implicitHeight: flow.implicitHeight + root.px(20)

    //? XP-TM shows raw KiB digits
    function kib(b) { return Math.floor(b / 1024) }
    function mib(b) { return Math.floor(b / 1048576) }

    //? Proportional unit: 1u = 1px in the 433px-wide reference screenshot
    readonly property real u: width / 431
    function px(v) { return Math.max(1, Math.round(v * u)) }

    //? Soft palette — chrome and traces come from the app theme; screens
    //? stay dark (the display surface)
    readonly property color face: Theme.windowBg
    readonly property color card: Theme.cardBgAlt
    readonly property color faceText: Theme.text
    readonly property color captionText: Theme.textDim
    readonly property color screen: "#10150f"
    readonly property color screenSofter: "#162016"

    //? OS accent (QPalette::Accent via SystemPalette) — the single ink for
    //? every equalizer bar and history line; track the user's OS setting
    SystemPalette {
        id: osPalette
    }
    readonly property color ink: osPalette.accent

    //? CPU equalizer bars: one fraction per core (color comes from root.ink)
    function coreBars() {
        const out = []
        for (let i = 0; i < CpuMonitor.perCore.length; ++i)
            out.push({ "fraction": CpuMonitor.perCore[i] / 100.0, "color": root.ink })
        return out
    }

    //? RAM/SWAP equalizer bars: RAM used + swap used (swap omitted when none)
    function memBars() {
        const ram = MemMonitor.total > 0 ? MemMonitor.used / MemMonitor.total : 0
        const out = [{ "fraction": ram, "color": root.ink }]
        if (MemMonitor.hasSwap && MemMonitor.swapTotal > 0)
            out.push({ "fraction": MemMonitor.swapUsed / MemMonitor.swapTotal,
                       "color": root.ink })
        return out
    }

    //? Borderless card: header caption on top, content flows below it via
    //? the layout — consumers never position against the caption by hand
    component WinTmCard: Rectangle {
        id: cardBox
        property string caption
        default property alias content: cardBody.data

        color: root.card
        radius: root.px(8)
        implicitWidth: cardBody.implicitWidth + root.px(20)
        implicitHeight: cardBody.implicitHeight + root.px(20)

        ColumnLayout {
            id: cardBody
            anchors.fill: parent
            anchors.margins: root.px(10)
            spacing: root.px(6)

            Label {
                text: cardBox.caption
                font.pixelSize: root.px(13)
                color: root.captionText
            }
        }
    }

    //? Soft-dark screen surface with the trace/grid content
    component WinTmScreen: Rectangle {
        color: root.screen
        radius: root.px(4)
    }

    //? Gauge: matrix screen; bar list as a column equalizer (narrow
    //? vertical bars); falls back to a single total level bar for scalar
    //? data; charts only — no value labels (numbers live in stat boxes)
    component WinTmGauge: WinTmCard {
        id: gaugeRoot
        property real fraction: 0.0
        property var bars: [] //? [{ "fraction": 0..1, "color": color }]; empty -> scalar fallback
        property string overlay: "" //? ink text inside the screen (charts-only)

        WinTmScreen {
            Layout.fillWidth: true
            Layout.fillHeight: true

            //? LED dot matrix
            Canvas {
                anchors.fill: parent
                anchors.margins: root.px(4)
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    ctx.fillStyle = "#143936"
                    const step = root.px(5)
                    for (let y = root.px(2); y < height; y += step)
                        for (let x = root.px(2); x < width; x += step)
                            ctx.fillRect(x, y, root.px(2), root.px(2))
                }
            }

            //? Equalizer — stacked horizontal dashes per bar (XP meter:
            //? short segments lit from the bottom up to the level)
            Canvas {
                id: eqCanvas
                anchors.fill: parent
                anchors.margins: root.px(4)
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    const bars = gaugeRoot.bars
                    const n = bars.length
                    if (n === 0)
                        return
                    const vstep = root.px(5)
                    const dashH = root.px(2)
                    const off = root.px(2)
                    const slotW = width / n
                    for (let i = 0; i < n; ++i) {
                        const frac = Math.min(Math.max(bars[i].fraction, 0), 1)
                        if (frac <= 0)
                            continue
                        //? Short centered dash — discrete column per bar, cores
                        //? never bleed into each other
                        const dashW = Math.min(slotW - root.px(2), root.px(10))
                        const gx = i * slotW + (slotW - dashW) / 2
                        //? Always show at least the bottom dash
                        const litFrac = frac < 1 ? Math.max(frac, 1 - dashH / (height - off)) : 1
                        const level = height - litFrac * (height - off)
                        ctx.fillStyle = bars[i].color
                        for (let y = off; y + dashH <= height; y += vstep) {
                            if (y >= level)
                                ctx.fillRect(gx, y, dashW, dashH)
                        }
                    }
                }
            }
            Connections {
                target: gaugeRoot
                function onBarsChanged() { eqCanvas.requestPaint() }
            }

            //? Ink overlay text inside the screen (top-left) — width-bound
            //? and shrunk to fit, never leaves the screen
            Label {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: root.px(6)
                width: parent.width - root.px(12)
                elide: Text.ElideRight
                fontSizeMode: Text.Fit
                minimumPixelSize: 4
                font.pixelSize: Math.min(root.px(12), Math.max(4, Math.round(parent.height * 0.20)))
                text: gaugeRoot.overlay
                color: root.ink
            }

            //? Total level bar (fallback for scalar data)
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: root.px(4)
                height: root.px(5)
                visible: gaugeRoot.bars.length === 0
                color: root.screenSofter
                Rectangle {
                    anchors.left: parent.left
                    width: parent.width * gaugeRoot.fraction
                    height: parent.height
                    radius: root.px(1)
                    color: root.ink
                }
            }
        }
    }

    //? Graph card: soft-dark screen + reused HistoryGraph (single or multi-series)
    component WinTmGraph: WinTmCard {
        id: graphRoot
        property list<double> samples: []
        property list<var> series: []
        property list<color> seriesColors: []
        property color lineColor: root.ink
        property string overlay: "" //? ink text inside the screen (top-left)

        WinTmScreen {
            Layout.fillWidth: true
            Layout.fillHeight: true
            HistoryGraph {
                anchors.fill: parent
                anchors.margins: root.px(4)
                samples: graphRoot.samples
                series: graphRoot.series
                seriesColors: graphRoot.seriesColors
                maxValue: 100
                lineColor: graphRoot.lineColor
                gridColor: Qt.rgba(graphRoot.lineColor.r, graphRoot.lineColor.g, graphRoot.lineColor.b, 0.06) //? hairline
                gridDivisions: 6
                verticalDivisions: Math.max(1, Math.round(width / 26))
            }
            //? Ink overlay text inside the screen (top-left) — width-bound
            //? and shrunk to fit, never leaves the screen
            Label {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: root.px(6)
                width: parent.width - root.px(12)
                elide: Text.ElideRight
                fontSizeMode: Text.Fit
                minimumPixelSize: 4
                font.pixelSize: Math.min(root.px(12), Math.max(4, Math.round(parent.height * 0.20)))
                text: graphRoot.overlay
                color: root.ink
            }
        }
    }

    //? Stat card: content-sized — compact fixed-pitch rows, card height
    //? follows its content (never stretches)
    component WinTmStatBox: WinTmCard {
        id: statRoot
        property var entries: []

        Repeater {
            model: statRoot.entries
            delegate: Item {
                id: statRow
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredHeight: root.px(20)
                Label {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: statRow.modelData.name
                    font.pixelSize: root.px(14)
                    color: root.faceText
                }
                Label {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: statRow.modelData.value
                    font.pixelSize: root.px(14)
                    color: root.faceText
                }
            }
        }
    }

    //? Status bar: ONE surface with cells separated by dividers
    //? (the reference's continuous sunken bar, translated borderless)
    component WinTmStatusBar: Rectangle {
        id: statusBar
        property var cells: [] //? [{ "text": string, "fill": bool }]

        implicitHeight: root.px(24)
        color: root.card
        radius: root.px(6)

        RowLayout {
            anchors.fill: parent
            spacing: 0
            Repeater {
                model: statusBar.cells
                delegate: Item {
                    id: statusCell
                    required property var modelData
                    required property int index
                    Layout.fillWidth: statusCell.modelData.fill === true
                    Layout.preferredWidth: cellText.implicitWidth + root.px(24)
                    Layout.fillHeight: true
                    Label {
                        id: cellText
                        anchors.centerIn: parent
                        text: statusCell.modelData.text
                        font.pixelSize: root.px(12)
                        color: root.faceText
                    }
                    Rectangle {
                        visible: statusCell.index < statusBar.cells.length - 1
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 1
                        height: parent.height - root.px(10)
                        color: root.face
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: root.face

        //? Single owner of the vertical flow: chart band (fixed) ->
        //? stat grid (fills the remainder) -> status bar (natural height)
        ColumnLayout {
            id: flow
            anchors.fill: parent
            //? Edge gap == block gap: the shell owns an 8px frame inset, the
            //? panel tops it up so shell+margin == spacing at any scale
            anchors.margins: Math.max(0, root.px(10) - 8)
            spacing: root.px(10)

            //? Top half — charts: bottom of panel reserved equally for
            //? the stat half (stat grid + status bar scrolled out only
            //? if the window's own minimum allows it)
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: root.px(120)
                GridLayout {
                    anchors.fill: parent
                    columns: 2
                    rowSpacing: root.px(10)
                    columnSpacing: root.px(10)

                    WinTmGauge {
                        caption: qsTr("CPU Usage")
                        bars: root.coreBars()
                        overlay: CpuMonitor.usage + " %"
                        Layout.preferredWidth: root.px(115)
                        Layout.fillHeight: true
                    }
                    WinTmGraph {
                        caption: qsTr("CPU Usage History")
                        samples: CpuMonitor.history
                        overlay: CpuMonitor.usage + " %"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }

                    WinTmGauge {
                        caption: qsTr("Memory Usage")
                        bars: root.memBars()
                        overlay: root.mib(MemMonitor.used) + " MB"
                        Layout.preferredWidth: root.px(115)
                        Layout.fillHeight: true
                    }
                    WinTmGraph {
                        caption: qsTr("Memory Usage History")
                        samples: MemMonitor.history
                        series: MemMonitor.hasSwap
                                ? [MemMonitor.history, MemMonitor.swapHistory] : []
                        seriesColors: MemMonitor.hasSwap ? [root.ink, root.ink] : []
                        overlay: root.mib(MemMonitor.used) + " MB"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }
                }
            }

            //? Bottom half — stat grid fills it, status bar keeps natural height
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: root.px(120)
                spacing: root.px(10)

                //? Stat cards — 2×2 grid, fills the bottom half
                GridLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    columns: 2
                    rowSpacing: root.px(10)
                    columnSpacing: root.px(10)

                WinTmStatBox {
                    caption: qsTr("Totals")
                    entries: [
                        { "name": qsTr("Handles"),   "value": CpuMonitor.handles },
                        { "name": qsTr("Threads"),   "value": ProcessModel.threadsTotal },
                        { "name": qsTr("Processes"), "value": ProcessModel.totalProcs }
                    ]
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
                WinTmStatBox {
                    caption: qsTr("Physical Memory (K)")
                    entries: [
                        { "name": qsTr("Total"),       "value": root.kib(MemMonitor.total) },
                        { "name": qsTr("Available"),   "value": root.kib(MemMonitor.available) },
                        { "name": qsTr("System Cache"),"value": root.kib(MemMonitor.cached) }
                    ]
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
                WinTmStatBox {
                    caption: qsTr("Commit Charge (K)")
                    entries: [
                        { "name": qsTr("Total"), "value": root.kib(MemMonitor.commitAS) },
                        { "name": qsTr("Limit"), "value": root.kib(MemMonitor.commitLimit) },
                        { "name": qsTr("Peak"),  "value": root.kib(MemMonitor.commitPeak) }
                    ]
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
                WinTmStatBox {
                    caption: qsTr("Kernel Memory (K)")
                    entries: [
                        { "name": qsTr("Total"),   "value": root.kib(MemMonitor.kernelSlab) },
                        { "name": qsTr("Paged"),   "value": root.kib(MemMonitor.kernelReclaimable) },
                        { "name": qsTr("Nonpaged"),"value": root.kib(MemMonitor.kernelSlab - MemMonitor.kernelReclaimable) }
                    ]
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
            }

            //? Status bar — one bar, cells in the reference's proportions
            WinTmStatusBar {
                Layout.fillWidth: true
                Layout.minimumHeight: implicitHeight
                Layout.maximumHeight: implicitHeight
                cells: [
                    { "text": qsTr("Processes: %1").arg(ProcessModel.totalProcs), "fill": false },
                    { "text": qsTr("CPU Usage: %1 %").arg(CpuMonitor.usage), "fill": false },
                    { "text": qsTr("Commit Charge: %1M / %2M")
                          .arg(root.mib(MemMonitor.commitAS))
                          .arg(root.mib(MemMonitor.commitLimit)), "fill": true }
                ]
            }
            }
        }
    }
}
