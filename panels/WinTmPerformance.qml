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

    //? btop's Theme::g("cpu") analogue: lerp accentCpu -> accentHot by percent
    function heatColor(pct) {
        const t = Math.min(Math.max(pct, 0), 100) / 100.0
        return Qt.rgba(
            Theme.accentCpu.r + (Theme.accentHot.r - Theme.accentCpu.r) * t,
            Theme.accentCpu.g + (Theme.accentHot.g - Theme.accentCpu.g) * t,
            Theme.accentCpu.b + (Theme.accentHot.b - Theme.accentCpu.b) * t, 1)
    }

    //? CPU equalizer bars: one dict per core { "fraction": 0..1, "color": heat }
    function coreBars() {
        const out = []
        for (let i = 0; i < CpuMonitor.perCore.length; ++i)
            out.push({ "fraction": CpuMonitor.perCore[i] / 100.0,
                       "color": root.heatColor(CpuMonitor.perCore[i]) })
        return out
    }

    //? RAM/SWAP equalizer bars: RAM used + swap used (swap omitted when none)
    function memBars() {
        const ram = MemMonitor.total > 0 ? MemMonitor.used / MemMonitor.total : 0
        const out = [{ "fraction": ram, "color": Theme.accentMem }]
        if (MemMonitor.hasSwap && MemMonitor.swapTotal > 0)
            out.push({ "fraction": MemMonitor.swapUsed / MemMonitor.swapTotal,
                       "color": Theme.accentWarn })
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
    //? data; value pinned at the bottom of the card flow
    component WinTmGauge: WinTmCard {
        id: gaugeRoot
        property string valueText
        property real fraction: 0.0
        property var bars: [] //? [{ "fraction": 0..1, "color": color }]; empty -> scalar fallback
        property color valueColor: Theme.text
        property color barColor: Theme.accentCpu

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

            //? Bar equalizer — narrow vertical bars, one per entry
            Item {
                anchors.fill: parent
                anchors.margins: root.px(4)
                visible: gaugeRoot.bars.length > 0
                Repeater {
                    model: gaugeRoot.bars
                    delegate: Item {
                        id: coreCol
                        required property var modelData
                        required property int index
                        readonly property real gap: root.px(1)
                        readonly property int barCount: gaugeRoot.bars.length
                        width: (parent.width - gap * (barCount - 1)) / barCount
                        height: parent.height
                        x: index * (width + gap)
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: parent.height * coreCol.modelData.fraction
                            color: coreCol.modelData.color
                        }
                    }
                }
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
                    color: gaugeRoot.barColor
                }
            }
        }

        Label {
            text: gaugeRoot.valueText
            Layout.alignment: Qt.AlignHCenter
            font.bold: true
            font.pixelSize: root.px(19)
            color: gaugeRoot.valueColor
        }
    }

    //? Graph card: soft-dark screen + reused HistoryGraph (single or multi-series)
    component WinTmGraph: WinTmCard {
        id: graphRoot
        property list<double> samples: []
        property list<var> series: []
        property list<color> seriesColors: []
        property color lineColor: Theme.accentCpu

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

            //? Chart band: fixed-compact (Item carries the layout attachment;
            //? the grid fills it)
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: root.px(235)
                Layout.minimumHeight: root.px(170)
                Layout.fillHeight: true
                GridLayout {
                    anchors.fill: parent
                    columns: 2
                    rowSpacing: root.px(10)
                    columnSpacing: root.px(10)

                    WinTmGauge {
                        caption: qsTr("CPU Usage")
                        valueText: CpuMonitor.usage + " %"
                        valueColor: Theme.accentCpu
                        bars: root.coreBars()
                        Layout.preferredWidth: root.px(115)
                        Layout.fillHeight: true
                    }
                    WinTmGraph {
                        caption: qsTr("CPU Usage History")
                        samples: CpuMonitor.history
                        lineColor: Theme.accentCpu
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }

                    WinTmGauge {
                        caption: qsTr("Memory Usage")
                        valueText: root.mib(MemMonitor.used) + " MB"
                        valueColor: Theme.accentMem
                        bars: root.memBars()
                        Layout.preferredWidth: root.px(115)
                        Layout.fillHeight: true
                    }
                    WinTmGraph {
                        caption: qsTr("Memory Usage History")
                        samples: MemMonitor.history
                        series: MemMonitor.hasSwap
                                ? [MemMonitor.history, MemMonitor.swapHistory] : []
                        seriesColors: MemMonitor.hasSwap
                                ? [Theme.accentMem, Theme.accentWarn] : []
                        lineColor: Theme.accentMem
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }
                }
            }

            //? Stat cards — 2×2 grid, the panel's data area (content-sized,
            //? never stretched or shrunk; the chart band absorbs the flex)
            GridLayout {
                Layout.fillWidth: true
                Layout.minimumHeight: implicitHeight
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
                    Layout.alignment: Qt.AlignTop
                }
                WinTmStatBox {
                    caption: qsTr("Physical Memory (K)")
                    entries: [
                        { "name": qsTr("Total"),       "value": root.kib(MemMonitor.total) },
                        { "name": qsTr("Available"),   "value": root.kib(MemMonitor.available) },
                        { "name": qsTr("System Cache"),"value": root.kib(MemMonitor.cached) }
                    ]
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                }
                WinTmStatBox {
                    caption: qsTr("Commit Charge (K)")
                    entries: [
                        { "name": qsTr("Total"), "value": root.kib(MemMonitor.commitAS) },
                        { "name": qsTr("Limit"), "value": root.kib(MemMonitor.commitLimit) },
                        { "name": qsTr("Peak"),  "value": root.kib(MemMonitor.commitPeak) }
                    ]
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                }
                WinTmStatBox {
                    caption: qsTr("Kernel Memory (K)")
                    entries: [
                        { "name": qsTr("Total"),   "value": root.kib(MemMonitor.kernelSlab) },
                        { "name": qsTr("Paged"),   "value": root.kib(MemMonitor.kernelReclaimable) },
                        { "name": qsTr("Nonpaged"),"value": root.kib(MemMonitor.kernelSlab - MemMonitor.kernelReclaimable) }
                    ]
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
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
