import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vostop

Rectangle {
    id: root

    color: "#222222"
    border.color: "#444444"
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
            color: "#e0e0e0"
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
                color: "#81c784"
            }
            Item { Layout.fillWidth: true }
            Label {
                visible: MemMonitor.total > 0
                text: Math.round(MemMonitor.used * 100 / MemMonitor.total) + "%"
                color: "#aaaaaa"
                font.pixelSize: 12
            }
        }

        ProgressBar {
            Layout.fillWidth: true
            from: 0
            to: 100
            value: MemMonitor.total > 0 ? MemMonitor.used * 100 / MemMonitor.total : 0

            background: Rectangle {
                implicitHeight: 10
                color: "#333333"
                radius: 3
            }
            contentItem: Item {
                implicitHeight: 10
                Rectangle {
                    width: (MemMonitor.total > 0 ? MemMonitor.used * 100 / MemMonitor.total : 0) / 100 * parent.width
                    height: parent.height
                    radius: 3
                    color: "#81c784"
                }
            }
        }

        HistoryGraph {
            Layout.fillWidth: true
            Layout.fillHeight: true
            samples: MemMonitor.history
            maxValue: 100.0
            lineColor: "#81c784"
        }

        Label {
            visible: MemMonitor.hasSwap
            text: qsTr("swap %1 / %2")
                .arg(root.fmtBytes(MemMonitor.swapUsed))
                .arg(root.fmtBytes(MemMonitor.swapTotal))
            color: "#aaaaaa"
            font.pixelSize: 10
        }

        //? PSI pressure sub-line (parity addition; hidden when PSI absent)
        Label {
            visible: MemMonitor.pressureValid
            text: {
                if (!visible)
                    return ""
                let s = qsTr("psi some %1%").arg(MemMonitor.pressureSome[0].toFixed(1))
                if (MemMonitor.pressureFull.length > 0 && MemMonitor.pressureFull[0] > 0)
                    s += qsTr(" · full %1%").arg(MemMonitor.pressureFull[0].toFixed(1))
                return s
            }
            color: "#c9a227"
            font.pixelSize: 10
        }
    }
}