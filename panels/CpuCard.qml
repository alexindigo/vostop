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

    //? Per-core grid view toggle (Win TM "logical processors" look; phase 6)
    property bool coreGrid: false

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("CPU")
                font.bold: true
                font.pixelSize: 14
                color: Theme.text
            }
            Label {
                text: CpuMonitor.cpuName
                color: Theme.textFaint
                font.pixelSize: 11
                elide: Text.ElideRight
            }
            Item { Layout.fillWidth: true }
            Button {
                text: root.coreGrid ? qsTr("graph") : qsTr("cores")
                font.pixelSize: 9
                flat: true
                onClicked: root.coreGrid = !root.coreGrid
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: CpuMonitor.usage + "%"
                font.pixelSize: 22
                font.bold: true
                color: Theme.accentCpu
            }
            Item { Layout.fillWidth: true }
            Label {
                text: CpuMonitor.freqText
                color: Theme.textDim
                font.pixelSize: 12
            }
        }

        HistoryGraph {
            Layout.fillWidth: true
            Layout.fillHeight: true
            samples: CpuMonitor.history
            maxValue: 100.0
            lineColor: Theme.accentCpu
        }

        //? Per-core presentation: bars (default) or Win-TM-style mini-graph grid
        StackLayout {
            id: coreStack
            Layout.fillWidth: true
            currentIndex: root.coreGrid ? 1 : 0

            GridLayout {
                columns: 4
                columnSpacing: 4
                rowSpacing: 3

                Repeater {
                    model: CpuMonitor.perCore.length

                    ColumnLayout {
                        id: coreCell
                        required property int index
                        spacing: 1

                        ProgressBar {
                            id: coreBar
                            Layout.fillWidth: true
                            from: 0
                            to: 100
                            value: CpuMonitor.perCore[coreCell.index] ?? 0

                            background: Rectangle {
                                implicitHeight: 5
                                color: Theme.innerBg
                                radius: 2
                            }
                            contentItem: Item {
                                implicitHeight: 5
                                Rectangle {
                                    width: coreBar.visualPosition * parent.width
                                    height: parent.height
                                    radius: 2
                                    color: Theme.accentCpu
                                }
                            }
                        }
                        Label {
                            text: coreCell.index
                            font.pixelSize: 8
                            color: Theme.textGhost
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }
                }
            }

            GridLayout {
                columns: 6
                columnSpacing: 3
                rowSpacing: 3

                Repeater {
                    model: CpuMonitor.coreHistories.length

                    ColumnLayout {
                        id: coreGraphCell
                        required property int index
                        required property var modelData
                        spacing: 0

                        HistoryGraph {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 30
                            samples: {
                                const ring = CpuMonitor.coreHistories[coreGraphCell.index]
                                return ring ? ring : []
                            }
                            maxValue: 100.0
                            lineColor: Theme.accentCpu
                            gridDivisions: 0
                        }
                        Label {
                            text: coreGraphCell.index
                            font.pixelSize: 7
                            color: Theme.textGhost
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("load %1 %2 %3  ·  up %4")
                .arg(CpuMonitor.load1.toFixed(2))
                .arg(CpuMonitor.load5.toFixed(2))
                .arg(CpuMonitor.load15.toFixed(2))
                .arg(root.formatUptime(CpuMonitor.uptimeSec))
            color: Theme.textFaint
            font.pixelSize: 10
        }

        //? PSI pressure: sub-line + sparkline (phase 6 styling); hidden when PSI absent
        RowLayout {
            visible: CpuMonitor.pressureValid
            Layout.fillWidth: true
            spacing: 6
            Label {
                text: {
                    if (!visible)
                        return ""
                    let s = qsTr("psi some %1%").arg(CpuMonitor.pressureSome[0].toFixed(1))
                    if (CpuMonitor.pressureFull.length > 0 && CpuMonitor.pressureFull[0] > 0)
                        s += qsTr(" · full %1%").arg(CpuMonitor.pressureFull[0].toFixed(1))
                    return s
                }
                color: CpuMonitor.pressureSome[0] > 30 ? Theme.accentDanger : Theme.accentWarn
                font.pixelSize: 10
            }
            HistoryGraph {
                Layout.fillWidth: true
                Layout.preferredHeight: 16
                visible: CpuMonitor.pressureHistory.length > 1
                samples: CpuMonitor.pressureHistory
                maxValue: 100.0
                lineColor: CpuMonitor.pressureSome[0] > 30 ? Theme.accentDanger : Theme.accentWarn
                gridDivisions: 0
            }
        }
    }

    function formatUptime(sec) {
        const s = Math.floor(sec)
        const d = Math.floor(s / 86400)
        const h = Math.floor((s % 86400) / 3600)
        const m = Math.floor((s % 3600) / 60)
        if (d > 0) return d + "d " + h + "h"
        if (h > 0) return h + "h " + m + "m"
        return m + "m"
    }
}