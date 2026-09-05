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

    function fmtRate(bps) {
        if (bps >= 1048576) return (bps / 1048576).toFixed(1) + " MiB/s"
        if (bps >= 1024) return (bps / 1024).toFixed(0) + " KiB/s"
        return bps.toFixed(0) + " B/s"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("Network")
                font.bold: true
                font.pixelSize: 14
                color: Theme.text
            }
            Item { Layout.fillWidth: true }
            //? iface picker
            ComboBox {
                id: ifacePicker
                font.pixelSize: 10
                model: ["auto"].concat(NetMonitor.ifaces)
                displayText: NetMonitor.iface === "" ? qsTr("auto") : NetMonitor.iface
                onActivated: function (idx) {
                    const sel = model[idx]
                    NetMonitor.selectIface(sel === "auto" ? "" : sel)
                }
            }
        }

        Label {
            text: NetMonitor.iface !== ""
                ? (NetMonitor.connected
                    ? qsTr("%1 · %2").arg(NetMonitor.iface).arg(NetMonitor.ipv4 || NetMonitor.ipv6 || qsTr("no ip"))
                    : qsTr("%1 · disconnected").arg(NetMonitor.iface))
                : qsTr("no interfaces")
            color: Theme.textFaint
            font.pixelSize: 10
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 0
                Label { text: qsTr("down"); color: Theme.accentCpu; font.pixelSize: 10 }
                Label { text: root.fmtRate(NetMonitor.downSpeed); color: Theme.accentCpu; font.bold: true; font.pixelSize: 13 }
            }
            Item { Layout.fillWidth: true }
            ColumnLayout {
                spacing: 0
                Label { text: qsTr("up"); color: Theme.accentMem; font.pixelSize: 10; Layout.alignment: Qt.AlignRight }
                Label { text: root.fmtRate(NetMonitor.upSpeed); color: Theme.accentMem; font.bold: true; font.pixelSize: 13; Layout.alignment: Qt.AlignRight }
            }
        }

        HistoryGraph {
            Layout.fillWidth: true
            Layout.fillHeight: true
            samples: NetMonitor.downHistory
            maxValue: Math.max(10240, Math.max(NetMonitor.downSpeed, NetMonitor.upSpeed) * 1.3)
            lineColor: "#4fc3f7"
        }

        HistoryGraph {
            Layout.fillWidth: true
            Layout.fillHeight: true
            samples: NetMonitor.upHistory
            maxValue: Math.max(10240, Math.max(NetMonitor.downSpeed, NetMonitor.upSpeed) * 1.3)
            lineColor: "#81c784"
        }

        Label {
            text: qsTr("total %1 ↓ · %2 ↑")
                .arg(root.fmtBytes(NetMonitor.downTotal))
                .arg(root.fmtBytes(NetMonitor.upTotal))
            color: Theme.textFaint
            font.pixelSize: 10
        }
    }
}