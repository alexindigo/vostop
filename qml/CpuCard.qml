import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vostop

Rectangle {
    id: root

    color: "#222222"
    border.color: "#444444"
    radius: 8

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
                color: "#e0e0e0"
            }
            Item { Layout.fillWidth: true }
            Label {
                text: CpuMonitor.cpuName
                color: "#888888"
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: CpuMonitor.usage + "%"
                font.pixelSize: 22
                font.bold: true
                color: "#4fc3f7"
            }
            Item { Layout.fillWidth: true }
            Label {
                text: CpuMonitor.freqText
                color: "#aaaaaa"
                font.pixelSize: 12
            }
        }

        HistoryGraph {
            Layout.fillWidth: true
            Layout.fillHeight: true
            samples: CpuMonitor.history
            maxValue: 100.0
            lineColor: "#4fc3f7"
        }

        //? Per-core bars (stolen per-core math; bars match `nproc`)
        GridLayout {
            Layout.fillWidth: true
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
                            color: "#333333"
                            radius: 2
                        }
                        contentItem: Item {
                            implicitHeight: 5
                            Rectangle {
                                width: coreBar.visualPosition * parent.width
                                height: parent.height
                                radius: 2
                                color: "#4fc3f7"
                            }
                        }
                    }
                    Label {
                        text: coreCell.index
                        font.pixelSize: 8
                        color: "#666666"
                        Layout.alignment: Qt.AlignHCenter
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
            color: "#888888"
            font.pixelSize: 10
        }

        //? PSI pressure sub-line (parity addition; hidden when PSI absent)
        Label {
            visible: CpuMonitor.pressureValid
            Layout.fillWidth: true
            text: {
                if (!visible)
                    return ""
                let s = qsTr("psi some %1%").arg(CpuMonitor.pressureSome[0].toFixed(1))
                if (CpuMonitor.pressureFull.length > 0 && CpuMonitor.pressureFull[0] > 0)
                    s += qsTr(" · full %1%").arg(CpuMonitor.pressureFull[0].toFixed(1))
                return s
            }
            color: "#c9a227"
            font.pixelSize: 10
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