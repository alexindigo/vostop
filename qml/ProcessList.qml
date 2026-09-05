pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vostop

Rectangle {
    id: root

    color: "#222222"
    border.color: "#444444"
    radius: 8

    property int sortColumn: ProcessModel.CpuCol
    property bool sortDesc: true

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
        proxy.sortBy(root.sortColumn, root.sortDesc)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        //? Header: title + totals + search + pause
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("Processes")
                font.bold: true
                font.pixelSize: 14
                color: "#e0e0e0"
            }
            Label {
                text: ProcessModel.totalProcs + qsTr(" procs · ") + ProcessModel.threadsTotal + qsTr(" threads")
                color: "#888888"
                font.pixelSize: 10
            }
            Item { Layout.fillWidth: true }
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
                placeholderText: qsTr("search")
                font.pixelSize: 11
                implicitWidth: 160
            }
        }

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
            ScrollBar.vertical: ScrollBar {}

            columnWidthProvider: function (col) {
                const base = [220, 52, 84, 90, 38, 44, 54]
                return ProcessModel.gpuColumnVisible ? (col === ProcessModel.GpuCol ? 48 : base[col]) : base[col]
            }
            rowHeightProvider: function (row) { return 22 }

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
                                ? "#33404d" : (cellName.index % 2 === 0 ? "#222222" : "#262626")
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
                            color: cellName.model.state === "X" ? "#555555" : "#dddddd"
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
                                ? "#33404d" : (cellCpu.index % 2 === 0 ? "#222222" : "#262626")
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
                                const v = Math.max(0, Math.min(100, cellCpu.model.cpuPct))
                                const a = 0.15 + 0.75 * (v / 100)
                                return Qt.rgba(0.31, 0.76, 0.97, cellCpu.model.cpuPct > 0 ? a : 0)
                            }
                        }
                        Label {
                            anchors.centerIn: parent
                            text: cellCpu.model.cpuPct > 0 ? cellCpu.model.cpuPct.toFixed(1) : ""
                            color: cellCpu.model.state === "X" ? "#555555" : "#dddddd"
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
                                ? "#33404d" : (cellMem.index % 2 === 0 ? "#222222" : "#262626")
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: ProcessModel.selectedPid = cellMem.model.pid
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            x: 4
                            text: root.fmtBytes(cellMem.model.memBytes)
                            color: cellMem.model.state === "X" ? "#555555" : "#bbbbbb"
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
                                ? "#33404d" : (cellUser.index % 2 === 0 ? "#222222" : "#262626")
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
                            color: "#999999"
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
                                ? "#33404d" : (cellThr.index % 2 === 0 ? "#222222" : "#262626")
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: ProcessModel.selectedPid = cellThr.model.pid
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            x: 4
                            text: cellThr.model.threads
                            color: "#999999"
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
                                ? "#33404d" : (cellState.index % 2 === 0 ? "#222222" : "#262626")
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: ProcessModel.selectedPid = cellState.model.pid
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            x: 4
                            text: cellState.model.state
                            color: cellState.model.state === "R" ? "#81c784"
                                : cellState.model.state === "X" ? "#555555" : "#999999"
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
                                ? "#33404d" : (cellPpid.index % 2 === 0 ? "#222222" : "#262626")
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: ProcessModel.selectedPid = cellPpid.model.pid
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            x: 4
                            text: cellPpid.model.ppid
                            color: "#888888"
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
                                ? "#33404d" : (cellGpu.index % 2 === 0 ? "#222222" : "#262626")
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: ProcessModel.selectedPid = cellGpu.model.pid
                        }
                        Label {
                            anchors.centerIn: parent
                            text: cellGpu.model.gpuPct >= 0 ? cellGpu.model.gpuPct.toFixed(1) : "—"
                            color: "#ce93d8"
                            font.pixelSize: 10
                        }
                    }
                }
            }
        }

        //? Detail readout for the selected pid (basic; presentation polish in phase 6)
        Rectangle {
            visible: ProcessModel.selectedPid !== 0
            Layout.fillWidth: true
            implicitHeight: detailCol.implicitHeight + 12
            color: "#1a1a1a"
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
                        color: "#dddddd"
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
                    //? SIGTERM → confirm → SIGKILL
                    Button {
                        text: qsTr("End Task")
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
                    color: "#aaaaaa"
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
                Label {
                    visible: ProcessModel.openFiles.length > 0
                    Layout.fillWidth: true
                    text: qsTr("open files: ") + ProcessModel.openFiles.slice(0, 8).join(", ")
                    color: "#777777"
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
            }
        }
    }

    //? Kill confirmation dialog (SIGTERM → confirm → SIGKILL)
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