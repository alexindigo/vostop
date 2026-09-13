pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vostop

//? XP Task Manager Performance tab — modernized: borderless rounded cards on
//? the theme's window background (no bevels), soft surfaces, LED-matrix
//? gauges with per-core segments, multi-core history lines, Linux-standard
//? Memory/Swap metrics instead of Page File. All type and chrome metrics
//? derive from u = width/431 (1u = 1px in the reference screenshot), so the
//? reference's text-to-panel proportions hold at any size.
Item {
    id: root
    property string panelTitle: qsTr("Win-TM Performance")

    //? XP-TM shows raw KiB digits
    function kib(b) { return Math.floor(b / 1024) }
    function mib(b) { return Math.floor(b / 1048576) }

    //? Proportional unit: 1u = 1px in the 433px-wide reference screenshot
    readonly property real u: width / 431
    function px(v) { return Math.max(1, Math.round(v * u)) }

    //? Soft palette — chrome follows the app theme; screens stay dark (the
    //? display surface) but softened off pure black; green/yellow are the
    //? XP-TM trace signature
    readonly property color face: Theme.windowBg
    readonly property color card: Theme.cardBgAlt
    readonly property color faceText: Theme.text
    readonly property color captionText: Theme.textDim
    readonly property color screen: "#10150f"
    readonly property color screenSofter: "#162016"
    readonly property color graphGreen: "#00e000"
    readonly property color graphYellow: "#e8e800"

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

    //? Gauge: matrix screen (with total or per-core segmented level bar);
    //? value pinned at the bottom of the card flow
    component WinTmGauge: WinTmCard {
        id: gaugeRoot
        property string valueText
        property real fraction: 0.0
        property var cores: [] //? latest percent per core

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
                    ctx.fillStyle = "#1d5a2a"
                    const step = root.px(5)
                    for (let y = root.px(2); y < height; y += step)
                        for (let x = root.px(2); x < width; x += step)
                            ctx.fillRect(x, y, root.px(2), root.px(2))
                }
            }

            //? Per-core segmented bar (falls back to total level bar)
            Row {
                id: coreStack
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: root.px(4)
                spacing: root.px(2)
                height: root.px(5)
                visible: gaugeRoot.cores.length > 0
                Repeater {
                    model: gaugeRoot.cores
                    delegate: Rectangle {
                        id: coreRow
                        required property var modelData
                        readonly property int coreCount: gaugeRoot.cores.length
                        width: (coreStack.width - (coreStack.spacing * (coreRow.coreCount - 1))) / coreRow.coreCount
                        height: parent.height
                        radius: root.px(1)
                        color: root.screenSofter
                        Rectangle {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width * (coreRow.modelData / 100.0)
                            height: parent.height
                            radius: parent.radius
                            color: root.graphGreen
                        }
                    }
                }
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: root.px(4)
                height: root.px(5)
                visible: gaugeRoot.cores.length === 0
                color: root.screenSofter
                Rectangle {
                    anchors.left: parent.left
                    width: parent.width * gaugeRoot.fraction
                    height: parent.height
                    radius: root.px(1)
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: "#0a7a30" }
                        GradientStop { position: 1.0; color: root.graphGreen }
                    }
                }
            }
        }

        Label {
            text: gaugeRoot.valueText
            Layout.alignment: Qt.AlignHCenter
            font.bold: true
            font.pixelSize: root.px(19)
            color: root.graphGreen
        }
    }

    //? Graph card: soft-dark screen + reused HistoryGraph (single or multi-series)
    component WinTmGraph: WinTmCard {
        id: graphRoot
        property list<double> samples: []
        property list<var> series: []
        property color lineColor: root.graphGreen

        WinTmScreen {
            Layout.fillWidth: true
            Layout.fillHeight: true
            HistoryGraph {
                anchors.fill: parent
                anchors.margins: root.px(4)
                samples: graphRoot.samples
                series: graphRoot.series
                maxValue: 100
                lineColor: graphRoot.lineColor
                fillColor: "transparent"
                gridColor: Qt.rgba(0, 0.85, 0, 0.25)
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
            anchors.fill: parent
            anchors.margins: root.px(8)
            spacing: root.px(10)

            //? Chart band: fixed-compact (Item carries the layout attachment;
            //? the grid fills it)
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: root.px(235)
                Layout.minimumHeight: root.px(180)
                Layout.fillHeight: true
                GridLayout {
                    anchors.fill: parent
                    columns: 2
                    rowSpacing: root.px(10)
                    columnSpacing: root.px(10)

                    WinTmGauge {
                        caption: qsTr("CPU Usage")
                        valueText: CpuMonitor.usage + " %"
                        fraction: CpuMonitor.usage / 100.0
                        cores: CpuMonitor.perCore
                        Layout.preferredWidth: root.px(115)
                        Layout.fillHeight: true
                    }
                    WinTmGraph {
                        caption: qsTr("CPU Usage History")
                        series: CpuMonitor.coreHistories
                        samples: CpuMonitor.history
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }

                    WinTmGauge {
                        caption: qsTr("Memory Usage")
                        valueText: root.mib(MemMonitor.used) + " MB"
                        fraction: MemMonitor.total > 0 ? MemMonitor.used / MemMonitor.total : 0.0
                        Layout.preferredWidth: root.px(115)
                        Layout.fillHeight: true
                    }
                    WinTmGraph {
                        caption: qsTr("Swap Usage History")
                        samples: MemMonitor.swapHistory
                        lineColor: root.graphYellow
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }
                }
            }

            //? Stat cards — 2×2 grid, the panel's data area (content-sized,
    //? never stretched; leftover space belongs to the chart band)
            GridLayout {
                Layout.fillWidth: true
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
