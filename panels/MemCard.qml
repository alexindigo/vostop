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

    function fmtBytes(b) {
        if (b >= 1073741824) return (b / 1073741824).toFixed(1) + " GiB"
        if (b >= 1048576) return (b / 1048576).toFixed(0) + " MiB"
        if (b >= 1024) return (b / 1024).toFixed(0) + " KiB"
        return b + " B"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        Label {
            text: qsTr("Memory")
            font.bold: true
            font.pixelSize: 14
            color: Theme.text
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: MemMonitor.used > 0
                    ? (MemMonitor.used / (1024 * 1024 * 1024)).toFixed(2) + " / "
                      + (MemMonitor.total / (1024 * 1024 * 1024)).toFixed(2) + " GiB"
                    : qsTr("no data yet")
                font.pixelSize: 16
                font.bold: true
                color: Theme.accentMem
            }
            Item { Layout.fillWidth: true }
            Label {
                visible: MemMonitor.total > 0
                text: Math.round(MemMonitor.used * 100 / MemMonitor.total) + "%"
                color: Theme.textDim
                font.pixelSize: 12
            }
        }

        //? Usage bar
        ProgressBar {
            Layout.fillWidth: true
            from: 0
            to: 100
            value: MemMonitor.total > 0 ? MemMonitor.used * 100 / MemMonitor.total : 0

            background: Rectangle {
                implicitHeight: 10
                color: Theme.innerBg
                radius: 3
            }
            contentItem: Item {
                implicitHeight: 10
                Rectangle {
                    width: (MemMonitor.total > 0 ? MemMonitor.used * 100 / MemMonitor.total : 0) / 100 * parent.width
                    height: parent.height
                    radius: 3
                    color: Theme.accentMem
                }
            }
        }

        //? Memory composition stacked bar (Win TM / macOS style; phase 6).
        //? Segments: in-use (total − available), cached, free — clamped to the bar.
        ColumnLayout {
            visible: MemMonitor.total > 0
            Layout.fillWidth: true
            spacing: 2

            Item {
                id: compBar
                Layout.fillWidth: true
                implicitHeight: 10

                Rectangle {
                    anchors.fill: parent
                    color: Theme.innerBg
                    radius: 3
                }
                Rectangle {
                    //? free segment (rightmost)
                    x: parent.width * Math.min(1, (MemMonitor.used + MemMonitor.cached) * 100 / MemMonitor.total / 100)
                    width: parent.width * Math.max(0, Math.min(1 - (MemMonitor.used + MemMonitor.cached) * 100 / MemMonitor.total / 100, 1))
                    height: parent.height
                    radius: 3
                    color: "#5a8f5c"
                    visible: MemMonitor.free > 0
                }
                Rectangle {
                    //? cached segment
                    x: parent.width * (MemMonitor.used * 100 / MemMonitor.total / 100)
                    width: parent.width * Math.max(0, MemMonitor.cached * 100 / MemMonitor.total / 100)
                    height: parent.height
                    color: "#4fc3f7"
                    opacity: 0.7
                    visible: MemMonitor.cached > 0
                }
                Rectangle {
                    //? in-use segment (left)
                    width: parent.width * Math.min(1, MemMonitor.used * 100 / MemMonitor.total / 100)
                    height: parent.height
                    radius: 3
                    color: Theme.accentMem
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Label {
                    text: qsTr("■ used %1").arg(root.fmtBytes(MemMonitor.used))
                    color: Theme.accentMem
                    font.pixelSize: 9
                }
                Label {
                    text: qsTr("■ cached %1").arg(root.fmtBytes(MemMonitor.cached))
                    color: Theme.accentCpu
                    font.pixelSize: 9
                    opacity: 0.8
                }
                Label {
                    text: qsTr("■ free %1").arg(root.fmtBytes(MemMonitor.free))
                    color: "#5a8f5c"
                    font.pixelSize: 9
                }
                Item { Layout.fillWidth: true }
            }
        }

        HistoryGraph {
            Layout.fillWidth: true
            Layout.fillHeight: true
            samples: MemMonitor.history
            maxValue: 100.0
            lineColor: Theme.accentMem
        }

        Label {
            visible: !MemMonitor.hasSwap
            text: qsTr("no swap configured")
            color: Theme.textGhost
            font.pixelSize: 10
        }

        Label {
            visible: MemMonitor.hasSwap
            text: qsTr("swap %1 / %2")
                .arg(root.fmtBytes(MemMonitor.swapUsed))
                .arg(root.fmtBytes(MemMonitor.swapTotal))
            color: Theme.textDim
            font.pixelSize: 10
        }

        //? PSI pressure: sub-line + sparkline (phase 6 styling)
        RowLayout {
            visible: MemMonitor.pressureValid
            Layout.fillWidth: true
            spacing: 6
            Label {
                text: {
                    if (!visible)
                        return ""
                    let s = qsTr("psi some %1%").arg(MemMonitor.pressureSome[0].toFixed(1))
                    if (MemMonitor.pressureFull.length > 0 && MemMonitor.pressureFull[0] > 0)
                        s += qsTr(" · full %1%").arg(MemMonitor.pressureFull[0].toFixed(1))
                    return s
                }
                color: MemMonitor.pressureSome[0] > 30 ? Theme.accentDanger : Theme.accentWarn
                font.pixelSize: 10
            }
            HistoryGraph {
                Layout.fillWidth: true
                Layout.preferredHeight: 16
                visible: MemMonitor.pressureHistory.length > 1
                samples: MemMonitor.pressureHistory
                maxValue: 100.0
                lineColor: MemMonitor.pressureSome[0] > 30 ? Theme.accentDanger : Theme.accentWarn
                gridDivisions: 0
            }
        }
    }
}