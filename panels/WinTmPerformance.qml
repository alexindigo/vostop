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

    //? XP-TM shows raw MiB digits in the header value
    function mib(b) { return Math.floor(b / 1048576) }

    //? btop-style humanizer, split: number + unit separately so stat rows
    //? can dim the unit (1024-based, one decimal below 10 of a unit)
    function humanBytesParts(b) {
        const units = ["B", "KB", "MB", "GB", "TB"]
        let v = b, i = 0
        while (v >= 1024 && i < units.length - 1) { v /= 1024; ++i }
        return { "v": (v < 10 && i > 0 ? v.toFixed(1) : Math.round(v)),
                 "u": units[i] }
    }

    //? One stat-row entry with a byte value + fraction ring
    function memEntry(name, bytes, total) {
        const p = humanBytesParts(bytes)
        return { "name": name, "value": p.v, "unit": p.u,
                 "fraction": total > 0 ? bytes / total : 0 }
    }
    function pct(part, total) {
        return total > 0 ? Math.round(part * 100 / total) + "%" : "—"
    }

    //? Proportional unit: 1u = 1px in the 433px-wide reference screenshot
    readonly property real u: width / 431
    function px(v) { return Math.max(1, Math.round(v * u)) }
    //? Stat-row line height — FIXED, declared once: rows never stretch or
    //? squeeze, pitch is identical in every box, air pools at card bottom
    readonly property int statRowH: px(16)

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

    //? Value units at 50% opacity
    readonly property color unitTint: Qt.rgba(faceText.r, faceText.g, faceText.b, 0.5)

    //? GPU stat rows, mirroring the Memory box: Usage / Memory Activity /
    //? Power with fraction rings (mean %, summed W across devices) + VRAM
    //? Used/Total; empty when no GPU data exists (box shows its emptyText)
    function gpuEntries() {
        const gpus = GpuMonitor.gpus
        if (gpus.length === 0)
            return []
        let utilSum = 0, utilN = 0
        let memUtilSum = 0, memUtilN = 0
        let powerMw = 0, powerMaxMw = 0
        let used = 0, total = 0
        for (let i = 0; i < gpus.length; ++i) {
            const g = gpus[i]
            if (g.util >= 0) { utilSum += g.util; ++utilN }
            if (g.memUtil >= 0) { memUtilSum += g.memUtil; ++memUtilN }
            if (g.powerMw > 0) { powerMw += g.powerMw; powerMaxMw += g.powerMaxMw }
            if (g.memTotal > 0) { used += g.memUsed; total += g.memTotal }
        }
        const out = []
        if (utilN > 0)
            out.push({ "name": qsTr("Usage:"), "value": Math.round(utilSum / utilN), "unit": "%",
                       "fraction": Math.min(utilSum / utilN / 100, 1) })
        if (memUtilN > 0)
            out.push({ "name": qsTr("Memory Activity:"), "value": Math.round(memUtilSum / memUtilN), "unit": "%",
                       "fraction": Math.min(memUtilSum / memUtilN / 100, 1) })
        if (total > 0) {
            const up = root.humanBytesParts(used)
            out.push({ "name": qsTr("Memory:"), "value": up.v, "unit": up.u,
                       "fraction": used / total })
        }
        if (powerMw > 0)
            out.push({ "name": qsTr("Power:"), "value": (powerMw / 1000).toFixed(1), "unit": "W",
                       "fraction": powerMaxMw > 0 ? Math.min(powerMw / powerMaxMw, 1) : 0 })
        return out
    }

    //? High-water ring scale for rates (session peak, 100 MB/s floor) —
    //? the max for disk I/O, and the fallback for network when the link
    //? speed is unknown (virtual ifaces report none)
    property real peakRead: 100 * 1048576
    property real peakWrite: 100 * 1048576
    property real peakDown: 100 * 1048576
    property real peakUp: 100 * 1048576

    function updateDiskPeaks() {
        let r = 0, w = 0
        const mounts = DiskMonitor.mounts
        for (let i = 0; i < mounts.length; ++i) {
            r += mounts[i].ioRead
            w += mounts[i].ioWrite
        }
        root.peakRead = Math.max(root.peakRead, r)
        root.peakWrite = Math.max(root.peakWrite, w)
    }

    Connections {
        target: DiskMonitor
        function onMountsChanged() { root.updateDiskPeaks() }
    }
    Connections {
        target: NetMonitor
        function onNetChanged() {
            root.peakDown = Math.max(root.peakDown, NetMonitor.downSpeed)
            root.peakUp = Math.max(root.peakUp, NetMonitor.upSpeed)
        }
    }

    //? Rate row: bytes/s humanized + fraction ring against the scale
    function rateEntry(name, bytesPerSec, scale) {
        const p = humanBytesParts(bytesPerSec)
        return { "name": name, "value": p.v, "unit": p.u + "/s",
                 "fraction": scale > 0 ? Math.min(bytesPerSec / scale, 1) : 0 }
    }

    //? Disk stat rows: root-fs usage with a ring, aggregate I/O rates,
    //? busiest-device io activity % with a ring (btop's disk data points)
    function diskEntries() {
        const mounts = DiskMonitor.mounts
        if (mounts.length === 0)
            return []
        let r = 0, w = 0, act = 0, rootUsed = 0, rootTotal = 0
        for (let i = 0; i < mounts.length; ++i) {
            const m = mounts[i]
            r += m.ioRead
            w += m.ioWrite
            if (m.ioActivity > act)
                act = m.ioActivity
            if (m.mountpoint === "/") {
                rootUsed = m.used
                rootTotal = m.total
            }
        }
        const out = []
        if (rootTotal > 0) {
            const p = root.humanBytesParts(rootUsed)
            out.push({ "name": qsTr("Usage:"), "value": p.v, "unit": p.u,
                       "fraction": rootUsed / rootTotal })
        }
        out.push(rateEntry(qsTr("Read:"),  r, root.peakRead))
        out.push(rateEntry(qsTr("Write:"), w, root.peakWrite))
        out.push({ "name": qsTr("Activity:"), "value": act, "unit": "%",
                   "fraction": Math.min(act / 100, 1) })
        return out
    }

    //? Network stat rows: down/up rates with rings against the link speed
    //? (high-water estimate when the iface reports no link speed)
    function netEntries() {
        if (!NetMonitor.connected)
            return []
        const downScale = NetMonitor.linkSpeed > 0 ? NetMonitor.linkSpeed : root.peakDown
        const upScale = NetMonitor.linkSpeed > 0 ? NetMonitor.linkSpeed : root.peakUp
        return [
            rateEntry(qsTr("Down:"), NetMonitor.downSpeed, downScale),
            rateEntry(qsTr("Up:"),   NetMonitor.upSpeed,   upScale)
        ]
    }

    //? Gauge card width: expand to fit bars at 2 dots/bar (up to 20 cores),
    //? 1 dot/bar beyond that; CPU and Memory expand together
    function gaugeWidth() {
        const n = CpuMonitor.perCore.length
        const dots = n > 20 ? 1 : 2
        return Math.max(root.px(115), n * dots * root.px(5) + root.px(28))
    }

    //? ONE width for both gauge cards — the wider of bar-fit and either
    //? card's header content; CPU and Memory are always the same width
    function gaugeRowWidth() {
        return Math.max(root.gaugeWidth(), cpuGauge.implicitWidth, memGauge.implicitWidth)
    }

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

    //? Borderless card: header caption on top (optional right-aligned value
    //? in ink), content flows below it via the layout — consumers never
    //? position against the caption by hand
    component WinTmCard: Rectangle {
        id: cardBox
        property string caption
        property string value: "" //? right-aligned header value ("" = hidden)
        property int titleSize: root.px(10)
        readonly property int bodySpacing: root.px(6)
        default property alias content: cardBody.data

        color: root.card
        radius: root.px(8)
        clip: true
        implicitWidth: cardBody.implicitWidth + root.px(20)
        implicitHeight: cardBody.implicitHeight + root.px(20)

        ColumnLayout {
            id: cardBody
            anchors.fill: parent
            anchors.margins: root.px(10)
            spacing: cardBox.bodySpacing

            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignBaseline
                    text: cardBox.caption
                    font.pixelSize: cardBox.titleSize
                    color: root.captionText
                    elide: Text.ElideRight
                }
                Label {
                    visible: cardBox.value.length > 0
                    Layout.alignment: Qt.AlignBaseline
                    text: cardBox.value
                    font.pixelSize: root.px(8)
                    color: root.ink
                }
            }
        }
    }

    //? Soft-dark screen surface with the trace/grid content. Natural
    //? height feeds the implicit-size chain (card -> grid -> panel ->
    //? window minimum), so the layout budget is always honest
    component WinTmScreen: Rectangle {
        implicitHeight: root.px(50)
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
                        //? Wide dash per row (the "-" stack), nearly the
                        //? full slot width — the 1901 reference look
                        const dashW = Math.max(dashH, slotW - root.px(2))
                        const gx = i * slotW + (slotW - dashW) / 2
                        //? Lit height = usage fraction; a non-zero bar always
                        //? lights at least one dash
                        const litH = Math.max(frac * (height - off), frac > 0 ? dashH + off : 0)
                        const level = height - litH
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

    //? Stat card: rows share the card height and shrink when squeezed —
    //? no row is ever pushed under the status bar. Optional per-row ring:
    //? entries may carry "fraction" (0..1) for a circular percentage icon;
    //? entries may carry "unit" — drawn tight after the value at 50%
    //? opacity. emptyText centers a note when there are no entries
    component WinTmStatBox: WinTmCard {
        id: statRoot
        property var entries: []
        property string emptyText: ""
        titleSize: root.px(10)

        Label {
            visible: statRoot.entries.length === 0 && statRoot.emptyText.length > 0
            text: statRoot.emptyText
            color: Theme.textGhost
            font.pixelSize: root.px(11)
            Layout.fillWidth: true
            Layout.fillHeight: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        Repeater {
            model: statRoot.entries
            delegate: Item {
                id: statRow
                required property var modelData
                //? Fixed line height via implicitHeight — Layout attached
                //? preferred/min/max are IGNORED on Repeater delegates
                //? (verified); no fillHeight, so rows never stretch and
                //? spare space pools at the card bottom
                implicitHeight: root.statRowH
                Layout.fillWidth: true
                clip: true

                //? Icon slot in front of every row: the circular
                //? percentage ring (palette track + accent arc); rows
                //? without a fraction leave the slot empty — the label
                //? indent is identical either way
                Canvas {
                    id: pctIcon
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: height
                    height: Math.min(root.px(10), statRow.height - root.px(4))
                    property real frac: statRow.modelData.fraction ?? 0
                    onFracChanged: requestPaint()
                    //? Canvas doesn't repaint on visibility/size changes by
                    //? itself — the ring must redraw when the row appears
                    //? or is resized, or it stays blank (startup race)
                    onVisibleChanged: if (visible) requestPaint()
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        if (statRow.modelData.fraction === undefined)
                            return  //? empty slot
                        const lw = root.px(2)
                        const r = Math.min(width, height) / 2 - lw / 2
                        const cx = width / 2
                        const cy = height / 2
                        ctx.lineWidth = lw
                        ctx.strokeStyle = Theme.innerBg
                        ctx.beginPath()
                        ctx.arc(cx, cy, r, 0, 2 * Math.PI)
                        ctx.stroke()
                        if (frac <= 0)
                            return
                        ctx.strokeStyle = root.ink
                        ctx.lineCap = "round"
                        ctx.beginPath()
                        ctx.arc(cx, cy, r, -Math.PI / 2,
                                -Math.PI / 2 + Math.min(frac, 1) * 2 * Math.PI)
                        ctx.stroke()
                    }
                }

                Label {
                    id: nameText
                    //? Gap icon->label = half the visible distance between
                    //? adjacent icons (row spacing + the row's slack around
                    //? the icon slot); identical for ring and bullet rows
                    anchors.left: pctIcon.right
                    anchors.leftMargin: (statRoot.bodySpacing + statRow.height - pctIcon.height) / 2
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: parent.width * 0.35
                    verticalAlignment: Text.AlignVCenter
                    fontSizeMode: Text.Fit
                    minimumPixelSize: 4
                    text: statRow.modelData.name
                    font.pixelSize: root.px(10)
                    color: root.faceText
                }
                //? value┊unit split: values right-align to the line, units
                //? left-align from it (smaller, dimmed) — the line is the
                //? same x for every row of the card
                Label {
                    id: valueText
                    anchors.left: nameText.right
                    anchors.right: unitText.left
                    anchors.rightMargin: root.px(1)
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                    fontSizeMode: Text.Fit
                    minimumPixelSize: 4
                    text: statRow.modelData.value
                    font.pixelSize: root.px(10)
                    color: root.faceText
                }
                Label {
                    id: unitText
                    anchors.right: parent.right
                    anchors.rightMargin: root.px(4)
                    anchors.baseline: valueText.baseline
                    width: root.px(16)
                    horizontalAlignment: Text.AlignLeft
                    fontSizeMode: Text.Fit
                    minimumPixelSize: 4
                    text: statRow.modelData.unit ?? ""
                    font.pixelSize: root.px(8)
                    color: root.unitTint
                }
            }
        }
        //? Owns the leftover card height — without it the layout
        //? spreads the spare space between the rows
        Item { Layout.fillHeight: true }
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

            //? Charts band — preferred height is the content's implicit
            //? height, not a magic number: the panel budget derives from
            //? what the cards actually contain
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: chartGrid.implicitHeight
                GridLayout {
                    id: chartGrid
                    anchors.fill: parent
                    columns: 2
                    rowSpacing: root.px(10)
                    columnSpacing: root.px(10)

                    WinTmGauge {
                        id: cpuGauge
                        caption: qsTr("CPU")
                        value: CpuMonitor.usage + " %"
                        bars: root.coreBars()
                        Layout.preferredWidth: root.gaugeRowWidth()
                        Layout.fillHeight: true
                    }
                    WinTmGraph {
                        caption: qsTr("CPU History")
                        samples: CpuMonitor.history
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }

                    WinTmGauge {
                        id: memGauge
                        caption: qsTr("Memory")
                        value: root.mib(MemMonitor.used) + " MB"
                        bars: root.memBars()
                        Layout.preferredWidth: root.gaugeRowWidth()
                        Layout.fillHeight: true
                    }
                    WinTmGraph {
                        caption: qsTr("Memory History")
                        samples: MemMonitor.history
                        series: MemMonitor.hasSwap
                                ? [MemMonitor.history, MemMonitor.swapHistory] : []
                        seriesColors: MemMonitor.hasSwap ? [root.ink, root.ink] : []
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }
                }
            }

            //? Bottom band — stat grid + status bar; preferred height is
            //? the content's implicit height (the layout's own), so the
            //? split between bands follows what the cards contain
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: implicitHeight
                spacing: root.px(10)
                clip: true

                //? Stat cards — 2×2 grid; rows share card height and shrink
                GridLayout {
                    id: statGrid
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    columns: 2
                    //? All cells the same width — card widths never jump
                    //? with the content
                    uniformCellWidths: true
                    rowSpacing: root.px(10)
                    columnSpacing: root.px(10)

                WinTmStatBox {
                    caption: qsTr("Memory")
                    entries: [
                        root.memEntry(qsTr("Used:"),      MemMonitor.used,      MemMonitor.total),
                        root.memEntry(qsTr("Available:"), MemMonitor.available, MemMonitor.total),
                        root.memEntry(qsTr("Cached:"),    MemMonitor.cached,    MemMonitor.total),
                        root.memEntry(qsTr("Free:"),      MemMonitor.free,      MemMonitor.total)
                    ]
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
                WinTmStatBox {
                    caption: qsTr("GPU")
                    entries: root.gpuEntries()
                    emptyText: qsTr("no supported GPU detected")
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
                WinTmStatBox {
                    caption: qsTr("Disk")
                    entries: root.diskEntries()
                    emptyText: qsTr("no mounts")
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
                WinTmStatBox {
                    caption: qsTr("Network")
                    entries: root.netEntries()
                    emptyText: qsTr("not connected")
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
            }

            //? Status bar — natural height, bottom-anchored, never covered
            WinTmStatusBar {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignBottom
                Layout.preferredHeight: implicitHeight
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
