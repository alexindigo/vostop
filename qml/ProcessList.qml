pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vostop

Rectangle {
    id: root

    color: Theme.cardBg
    border.color: Theme.cardBorder
    radius: 8

    property int sortColumn: {
        //? persisted (btop proc_sorting semantics; "cpu lazy" → cpu-desc)
        const key = Settings.procSorting
        const map = { "pid": ProcessModel.PpidCol, "program": ProcessModel.NameCol,
            "name": ProcessModel.NameCol, "command": ProcessModel.NameCol,
            "threads": ProcessModel.ThreadsCol, "user": ProcessModel.UserCol,
            "memory": ProcessModel.MemCol, "cpu direct": ProcessModel.CpuCol,
            "cpu lazy": ProcessModel.CpuCol }
        return map[key] !== undefined ? map[key] : ProcessModel.CpuCol
    }
    property bool sortDesc: true
    property string _sortingKey: Settings.procSorting
    property bool grouped: false //? grouped sections ↔ "Details" flat table (§9 Q7)
    property int currentRow: 0   //? keyboard nav anchor

    function fmtBytes(b) {
        if (b >= 1073741824) return (b / 1073741824).toFixed(1) + " GiB"
        if (b >= 1048576) return (b / 1048576).toFixed(0) + " MiB"
        if (b >= 1024) return (b / 1024).toFixed(0) + " KiB"
        return b + " B"
    }

    function fmtRate(bps) {
        if (bps >= 1048576) return (bps / 1048576).toFixed(1) + " MB/s"
        if (bps >= 1024) return (bps / 1024).toFixed(0) + " KB/s"
        return bps.toFixed(0) + " B/s"
    }

    ProcFilterProxyModel {
        id: proxy
        sourceModel: ProcessModel
        filter: searchField.text
        Component.onCompleted: proxy.sortBy(root.sortColumn, root.sortDesc)
    }

    function setSort(col) {
        if (root.sortColumn === col)
            root.sortDesc = !root.sortDesc
        else
            root.sortDesc = true
        root.sortColumn = col
        //? persist (phase 6): reverse of the column map above
        const names = {}
        names[ProcessModel.PpidCol] = "pid"
        names[ProcessModel.NameCol] = "name"
        names[ProcessModel.ThreadsCol] = "threads"
        names[ProcessModel.UserCol] = "user"
        names[ProcessModel.MemCol] = "memory"
        names[ProcessModel.CpuCol] = "cpu lazy"
        Settings.procSorting = names[col] ?? "cpu lazy"
        //? btop convention: cpu-desc == NOT reversed; text columns: desc == reversed
        Settings.procReversed = (col === ProcessModel.CpuCol) ? !root.sortDesc : root.sortDesc
        proxy.sortBy(root.sortColumn, root.sortDesc)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        //? Header: title + totals + grouped/flat toggle + search + pause
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("Processes")
                font.bold: true
                font.pixelSize: 14
                color: Theme.text
            }
            Label {
                text: ProcessModel.totalProcs + qsTr(" procs · ") + ProcessModel.threadsTotal + qsTr(" threads")
                color: Theme.textFaint
                font.pixelSize: 10
            }
            Item { Layout.fillWidth: true }
            Button {
                text: root.grouped ? qsTr("Details") : qsTr("Grouped")
                font.pixelSize: 10
                flat: true
                onClicked: root.grouped = !root.grouped
            }
            Button {
                id: pauseButton
                checkable: true
                checked: false
                text: checked ? qsTr("Resume") : qsTr("Pause")
                font.pixelSize: 10
                onCheckedChanged: Settings.pauseProcList = checked
            }
            TextField {
                id: searchField
                placeholderText: qsTr("search ( / )")
                font.pixelSize: 11
                implicitWidth: 160
                color: Theme.text
                text: Settings.procFilter
                onEditingFinished: Settings.procFilter = text
            }
            Button {
                checkable: true
                checked: Settings.procFilterKernel
                text: qsTr("hide kernel")
                font.pixelSize: 10
                onCheckedChanged: Settings.procFilterKernel = checked
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.grouped ? 1 : 0

            //? ─────────── Page 0: flat "Details" table ───────────
            ColumnLayout {
                spacing: 4

                //? Sort header — structurally synced to the table (same column widths)
                HorizontalHeaderView {
                    id: header
                    Layout.fillWidth: true
                    syncView: tableView
                    reuseItems: false
                    implicitHeight: 24

                    delegate: Button {
                        required property int column
                        required property var model
                        text: {
                            const arrow = (root.sortColumn === column)
                                ? (root.sortDesc ? "▼ " : "▲ ") : ""
                            return arrow + (model ? (model.display ?? "") : "")
                        }
                        font.pixelSize: 10
                        flat: true
                        palette.buttonText: Theme.textDim
                        onClicked: root.setSort(column)
                    }
                }

                //? Virtualized table (reuseItems: viewport-only recycled delegates)
                TableView {
                    id: tableView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: proxy
                    reuseItems: true
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    focus: true
                    ScrollBar.vertical: ScrollBar {}

                    columnWidthProvider: function (col) {
                        const base = [220, 52, 84, 90, 38, 44, 54]
                        return ProcessModel.gpuColumnVisible ? (col === ProcessModel.GpuCol ? 48 : base[col]) : base[col]
                    }
                    rowHeightProvider: function (row) { return 22 }

                    //? Keyboard navigation (phase 6): up/down move selection by pid,
                    //? "/" focuses search, "k" opens the kill confirmation
                    Keys.onPressed: function (event) {
                        if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                            const delta = event.key === Qt.Key_Up ? -1 : 1
                            const newRow = Math.max(0, Math.min(proxy.rowCount() - 1, root.currentRow + delta))
                            if (newRow !== root.currentRow) {
                                root.currentRow = newRow
                                const idx = proxy.index(newRow, 0)
                                ProcessModel.selectedPid = proxy.data(idx, ProcessModel.PidRole)
                            }
                            event.accepted = true
                        } else if (event.key === Qt.Key_Slash) {
                            searchField.focus = true
                            event.accepted = true
                        } else if (event.key === Qt.Key_K && ProcessModel.selectedPid !== 0) {
                            confirmDialog.open()
                            event.accepted = true
                        }
                    }

                    //? No-match empty state (phase 6)
                    Label {
                        anchors.centerIn: parent
                        visible: proxy.rowCount() === 0 && !searchField.text
                        text: qsTr("no processes")
                        color: Theme.textGhost
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: proxy.rowCount() === 0 && !!searchField.text
                        text: qsTr("no matching processes")
                        color: Theme.textGhost
                    }

                    delegate: DelegateChooser {
                        role: "display"

                        DelegateChoice {
                            column: ProcessModel.NameCol
                            Item {
                                id: cellName
                                required property var model
                                required property int index
                                implicitHeight: 22
                                Rectangle {
                                    anchors.fill: parent
                                    color: ProcessModel.selectedPid === cellName.model.pid
                                        ? Theme.selection : (cellName.index % 2 === 0 ? Theme.cardBg : Theme.cardBgAlt)
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: ProcessModel.selectedPid = cellName.model.pid
                                }
                                Label {
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: 2
                                    width: parent.width - 4
                                    text: cellName.model.name
                                    color: cellName.model.state === "X" ? Theme.textDead : Theme.textBright
                                    elide: Text.ElideRight
                                    font.pixelSize: 11
                                }
                            }
                        }
                        DelegateChoice {
                            column: ProcessModel.CpuCol
                            Item {
                                id: cellCpu
                                required property var model
                                required property int index
                                implicitHeight: 22
                                Rectangle {
                                    anchors.fill: parent
                                    color: ProcessModel.selectedPid === cellCpu.model.pid
                                        ? Theme.selection : (cellCpu.index % 2 === 0 ? Theme.cardBg : Theme.cardBgAlt)
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: ProcessModel.selectedPid = cellCpu.model.pid
                                }
                                //? CPU% heat-map cell
                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    color: {
                                        const a = Theme.heatAlpha(cellCpu.model.cpuPct)
                                        return Qt.rgba(0.31, 0.76, 0.97, cellCpu.model.cpuPct > 0 ? a : 0)
                                    }
                                }
                                Label {
                                    anchors.centerIn: parent
                                    text: cellCpu.model.cpuPct > 0 ? cellCpu.model.cpuPct.toFixed(1) : ""
                                    color: cellCpu.model.state === "X" ? Theme.textDead : Theme.textBright
                                    font.pixelSize: 10
                                }
                            }
                        }
                        DelegateChoice {
                            column: ProcessModel.MemCol
                            Item {
                                id: cellMem
                                required property var model
                                required property int index
                                implicitHeight: 22
                                Rectangle {
                                    anchors.fill: parent
                                    color: ProcessModel.selectedPid === cellMem.model.pid
                                        ? Theme.selection : (cellMem.index % 2 === 0 ? Theme.cardBg : Theme.cardBgAlt)
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: ProcessModel.selectedPid = cellMem.model.pid
                                }
                                Label {
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: 4
                                    text: root.fmtBytes(cellMem.model.memBytes)
                                    color: cellMem.model.state === "X" ? Theme.textDead : Theme.textDim
                                    font.pixelSize: 10
                                }
                            }
                        }
                        DelegateChoice {
                            column: ProcessModel.UserCol
                            Item {
                                id: cellUser
                                required property var model
                                required property int index
                                implicitHeight: 22
                                Rectangle {
                                    anchors.fill: parent
                                    color: ProcessModel.selectedPid === cellUser.model.pid
                                        ? Theme.selection : (cellUser.index % 2 === 0 ? Theme.cardBg : Theme.cardBgAlt)
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: ProcessModel.selectedPid = cellUser.model.pid
                                }
                                Label {
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: 4
                                    width: parent.width - 8
                                    text: cellUser.model.user
                                    color: Theme.textFaint
                                    elide: Text.ElideRight
                                    font.pixelSize: 10
                                }
                            }
                        }
                        DelegateChoice {
                            column: ProcessModel.ThreadsCol
                            Item {
                                id: cellThr
                                required property var model
                                required property int index
                                implicitHeight: 22
                                Rectangle {
                                    anchors.fill: parent
                                    color: ProcessModel.selectedPid === cellThr.model.pid
                                        ? Theme.selection : (cellThr.index % 2 === 0 ? Theme.cardBg : Theme.cardBgAlt)
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: ProcessModel.selectedPid = cellThr.model.pid
                                }
                                Label {
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: 4
                                    text: cellThr.model.threads
                                    color: Theme.textFaint
                                    font.pixelSize: 10
                                }
                            }
                        }
                        DelegateChoice {
                            column: ProcessModel.StateCol
                            Item {
                                id: cellState
                                required property var model
                                required property int index
                                implicitHeight: 22
                                Rectangle {
                                    anchors.fill: parent
                                    color: ProcessModel.selectedPid === cellState.model.pid
                                        ? Theme.selection : (cellState.index % 2 === 0 ? Theme.cardBg : Theme.cardBgAlt)
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: ProcessModel.selectedPid = cellState.model.pid
                                }
                                Label {
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: 4
                                    text: cellState.model.state
                                    color: cellState.model.state === "R" ? Theme.accentMem
                                        : cellState.model.state === "X" ? Theme.textDead : Theme.textFaint
                                    font.pixelSize: 10
                                }
                            }
                        }
                        DelegateChoice {
                            column: ProcessModel.PpidCol
                            Item {
                                id: cellPpid
                                required property var model
                                required property int index
                                implicitHeight: 22
                                Rectangle {
                                    anchors.fill: parent
                                    color: ProcessModel.selectedPid === cellPpid.model.pid
                                        ? Theme.selection : (cellPpid.index % 2 === 0 ? Theme.cardBg : Theme.cardBgAlt)
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: ProcessModel.selectedPid = cellPpid.model.pid
                                }
                                Label {
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: 4
                                    text: cellPpid.model.ppid
                                    color: Theme.textFaint
                                    font.pixelSize: 10
                                }
                            }
                        }
                        DelegateChoice {
                            column: ProcessModel.GpuCol
                            Item {
                                id: cellGpu
                                required property var model
                                required property int index
                                implicitHeight: 22
                                Rectangle {
                                    anchors.fill: parent
                                    color: ProcessModel.selectedPid === cellGpu.model.pid
                                        ? Theme.selection : (cellGpu.index % 2 === 0 ? Theme.cardBg : Theme.cardBgAlt)
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: ProcessModel.selectedPid = cellGpu.model.pid
                                }
                                Label {
                                    anchors.centerIn: parent
                                    text: cellGpu.model.gpuPct >= 0 ? cellGpu.model.gpuPct.toFixed(1) : "—"
                                    color: Theme.accentGpu
                                    font.pixelSize: 10
                                }
                            }
                        }
                    }
                }
            }

            //? ─────────── Page 1: grouped sections (§9 Q7/Q8) ───────────
            ListView {
                id: groupedList
                clip: true
                model: GroupedProcModel
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                section.property: "category"
                section.criteria: ViewSection.FullString
                //? Sticky section headers (§9 Q8: native to ListView)
                section.labelPositioning: ViewSection.CurrentLabelAtStart | ViewSection.InlineLabels

                section.delegate: Rectangle {
                    id: sectionHeader
                    required property string section
                    width: groupedList.width
                    height: 28
                    color: Theme.detailBg
                    z: 2

                    readonly property int cat: parseInt(sectionHeader.section)
                    readonly property bool collapsed: cat === 0 ? GroupedProcModel.appsCollapsed
                        : cat === 1 ? GroupedProcModel.backgroundCollapsed : GroupedProcModel.systemCollapsed

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 4
                        spacing: 6

                        Button {
                            text: sectionHeader.collapsed ? "▶" : "▼"
                            font.pixelSize: 9
                            flat: true
                            onClicked: {
                                if (sectionHeader.cat === 0) GroupedProcModel.appsCollapsed = !sectionHeader.collapsed
                                else if (sectionHeader.cat === 1) GroupedProcModel.backgroundCollapsed = !sectionHeader.collapsed
                                else GroupedProcModel.systemCollapsed = !sectionHeader.collapsed
                            }
                        }
                        Label {
                            text: sectionHeader.cat === 0 ? qsTr("Apps")
                                : sectionHeader.cat === 1 ? qsTr("Background") : qsTr("System")
                            color: Theme.text
                            font.bold: true
                            font.pixelSize: 11
                        }
                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.cardBorder }

                        //? Per-section sort (§9 Q8) — cycles column, then direction
                        ComboBox {
                            id: sectionSortPicker
                            font.pixelSize: 9
                            implicitWidth: 96
                            model: ["cpu", "memory", "name", "user", "threads", "pid"]
                            onActivated: function (idx) {
                                const cols = [ProcessModel.CpuCol, ProcessModel.MemCol, ProcessModel.NameCol,
                                    ProcessModel.UserCol, ProcessModel.ThreadsCol, ProcessModel.PpidCol]
                                GroupedProcModel.proxy(sectionHeader.cat)?.sortBy(cols[idx], true)
                            }
                        }
                        //? Per-section filter (§9 Q8)
                        TextField {
                            id: sectionFilterField
                            placeholderText: qsTr("filter")
                            font.pixelSize: 9
                            implicitWidth: 110
                            color: Theme.text
                            onTextChanged: {
                                const p = GroupedProcModel.proxy(sectionHeader.cat)
                                if (p) p.sectionFilter = text
                            }
                        }
                    }
                }

                //? Row delegate: fixed column positions (§9 Q8 ListView choice)
                delegate: Item {
                    id: groupRow
                    required property var model
                    required property int index
                    width: groupedList.width
                    height: 22

                    Rectangle {
                        anchors.fill: parent
                        color: ProcessModel.selectedPid === groupRow.model.pid
                            ? Theme.selection : (groupRow.index % 2 === 0 ? Theme.cardBg : Theme.cardBgAlt)
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: ProcessModel.selectedPid = groupRow.model.pid
                    }

                    RowLayout {
                        anchors.fill: parent
                        spacing: 0

                        Item {
                            Layout.preferredWidth: 220
                            Layout.fillHeight: true
                            Label {
                                anchors.verticalCenter: parent.verticalCenter
                                x: 2
                                width: parent.width - 4
                                text: groupRow.model.name
                                color: groupRow.model.state === "X" ? Theme.textDead : Theme.textBright
                                elide: Text.ElideRight
                                font.pixelSize: 11
                            }
                        }
                        Item {
                            Layout.preferredWidth: 52
                            Layout.fillHeight: true
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 1
                                color: {
                                    const a = Theme.heatAlpha(groupRow.model.cpuPct)
                                    return Qt.rgba(0.31, 0.76, 0.97, groupRow.model.cpuPct > 0 ? a : 0)
                                }
                            }
                            Label {
                                anchors.centerIn: parent
                                text: groupRow.model.cpuPct > 0 ? groupRow.model.cpuPct.toFixed(1) : ""
                                color: groupRow.model.state === "X" ? Theme.textDead : Theme.textBright
                                font.pixelSize: 10
                            }
                        }
                        Item {
                            Layout.preferredWidth: 84
                            Layout.fillHeight: true
                            Label {
                                anchors.verticalCenter: parent.verticalCenter
                                x: 4
                                text: root.fmtBytes(groupRow.model.memBytes)
                                color: groupRow.model.state === "X" ? Theme.textDead : Theme.textDim
                                font.pixelSize: 10
                            }
                        }
                        Item {
                            Layout.preferredWidth: 90
                            Layout.fillHeight: true
                            Label {
                                anchors.verticalCenter: parent.verticalCenter
                                x: 4
                                width: parent.width - 8
                                text: groupRow.model.user
                                color: Theme.textFaint
                                elide: Text.ElideRight
                                font.pixelSize: 10
                            }
                        }
                        Item {
                            Layout.preferredWidth: 38
                            Layout.fillHeight: true
                            Label {
                                anchors.verticalCenter: parent.verticalCenter
                                x: 4
                                text: groupRow.model.threads
                                color: Theme.textFaint
                                font.pixelSize: 10
                            }
                        }
                        Item {
                            Layout.preferredWidth: 44
                            Layout.fillHeight: true
                            Label {
                                anchors.verticalCenter: parent.verticalCenter
                                x: 4
                                text: groupRow.model.state
                                color: groupRow.model.state === "R" ? Theme.accentMem
                                    : groupRow.model.state === "X" ? Theme.textDead : Theme.textFaint
                                font.pixelSize: 10
                            }
                        }
                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: GroupedProcModel.rowCount() === 0 && !searchField.text
                    text: qsTr("no processes")
                    color: Theme.textGhost
                }
                Label {
                    anchors.centerIn: parent
                    visible: GroupedProcModel.rowCount() === 0 && !!searchField.text
                    text: qsTr("no matching processes")
                    color: Theme.textGhost
                }
            }
        }

        //? Detail readout for the selected pid (basic; presentation polish here)
        Rectangle {
            visible: ProcessModel.selectedPid !== 0
            Layout.fillWidth: true
            implicitHeight: detailCol.implicitHeight + 12
            color: Theme.detailBg
            radius: 6

            ColumnLayout {
                id: detailCol
                anchors.fill: parent
                anchors.margins: 6
                spacing: 2

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: ProcessModel.selectedPid !== 0
                            ? qsTr("pid %1").arg(ProcessModel.selectedPid)
                            : ""
                        color: Theme.textBright
                        font.bold: true
                        font.pixelSize: 11
                    }
                    Item { Layout.fillWidth: true }
                    //? Renice (own-user only)
                    SpinBox {
                        id: niceBox
                        from: -20
                        to: 19
                        value: 0
                        font.pixelSize: 10
                        editable: true
                    }
                    Button {
                        text: qsTr("renice")
                        font.pixelSize: 10
                        onClicked: ProcessModel.renice(ProcessModel.selectedPid, niceBox.value)
                    }
                    //? SIGTERM → confirm → SIGKILL (keyboard: k)
                    Button {
                        text: qsTr("End Task (k)")
                        font.pixelSize: 10
                        onClicked: confirmDialog.open()
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: {
                        if (ProcessModel.selectedPid === 0)
                            return ""
                        let parts = []
                        if (ProcessModel.detailStatus !== "") parts.push(ProcessModel.detailStatus)
                        parts.push(qsTr("%1 threads").arg(ProcessModel.detailThreads))
                        if (ProcessModel.detailParent !== "") parts.push(qsTr("ppid %1 (%2)").arg(ProcessModel.detailPpid).arg(ProcessModel.detailParent))
                        if (ProcessModel.detailElapsed !== "") parts.push(qsTr("elapsed %1").arg(ProcessModel.detailElapsed))
                        if (ProcessModel.detailMemBytes > 0) parts.push(root.fmtBytes(ProcessModel.detailMemBytes))
                        return parts.join(" · ")
                    }
                    color: Theme.textDim
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
                Label {
                    visible: ProcessModel.openFiles.length > 0
                    Layout.fillWidth: true
                    text: qsTr("open files: ") + ProcessModel.openFiles.slice(0, 8).join(", ")
                    color: Theme.textFaint
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
            }
        }
    }

    //? Kill confirmation dialog (SIGTERM → confirm → SIGKILL; keyboard: k)
    Dialog {
        id: confirmDialog
        title: qsTr("End process?")
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        parent: Overlay.overlay
        anchors.centerIn: parent
        Label {
            text: ProcessModel.selectedPid !== 0
                ? qsTr("Send SIGTERM to %1 (pid %2)?")
                    .arg(ProcessModel.detailName)
                    .arg(ProcessModel.selectedPid)
                : ""
            color: Theme.text
        }
        onAccepted: {
            if (ProcessModel.killProcess(ProcessModel.selectedPid, 15)) {
                //? Grace period, then SIGKILL if still alive
                killEscalateTimer.pid = ProcessModel.selectedPid
                killEscalateTimer.start()
            }
        }
    }

    Timer {
        id: killEscalateTimer
        property int pid: 0
        interval: 3000
        onTriggered: {
            if (pid !== 0)
                ProcessModel.killProcess(pid, 9)
            pid = 0
        }
    }
}