pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vostop
import Top

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

    //? Active interfaces (user display decision 2026-09-25): connected, an
    //? assigned IPv4/IPv6, not loopback; the 172.17.0.0/16 range (docker0's)
    //? is hidden while the persisted netHideDocker toggle is on. Rebuilt on
    //? the model's per-tick reset — the row count is tiny, a JS walk is fine.
    property var netRows: []
    //? Graphs stick to one iface by name while it stays in the filtered set
    //? (first row otherwise), so quiet ifaces don't flip the history
    property string shownIface: ""
    property var shownRow: null

    function isActiveIface(row) {
        if (!row.connected)
            return false
        if (row.ipv4.length === 0 && row.ipv6.length === 0)
            return false
        if (row.name === "lo")
            return false
        if (Settings.netHideDocker && row.ipv4.startsWith("172.17."))
            return false
        return true
    }

    function rebuildNet() {
        const m = NetIfacesModel
        const out = []
        for (let i = 0; i < m.rowCount(); ++i) {
            const idx = m.index(i, 0)
            const row = {
                "name": m.data(idx, NetIfacesModel.NameRole),
                "ipv4": m.data(idx, NetIfacesModel.Ipv4Role),
                "ipv6": m.data(idx, NetIfacesModel.Ipv6Role),
                "connected": m.data(idx, NetIfacesModel.ConnectedRole),
                "downSpeed": m.data(idx, NetIfacesModel.DownSpeedRole),
                "upSpeed": m.data(idx, NetIfacesModel.UpSpeedRole),
                "downTotal": m.data(idx, NetIfacesModel.DownTotalRole),
                "upTotal": m.data(idx, NetIfacesModel.UpTotalRole),
                "downHistory": m.data(idx, NetIfacesModel.DownHistoryRole),
                "upHistory": m.data(idx, NetIfacesModel.UpHistoryRole)
            }
            if (root.isActiveIface(row))
                out.push(row)
        }
        netRows = out
        let shown = null
        for (const r of out) {
            if (r.name === shownIface) {
                shown = r
                break
            }
        }
        if (shown === null)
            shown = out.length > 0 ? out[0] : null
        shownIface = shown !== null ? shown.name : ""
        shownRow = shown
    }

    Connections {
        target: NetIfacesModel
        function onModelReset() { root.rebuildNet() }
    }
    Connections {
        target: Settings
        function onNetHideDockerChanged() { root.rebuildNet() }
    }
    Component.onCompleted: root.rebuildNet()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        Label {
            text: qsTr("Network")
            font.bold: true
            font.pixelSize: 14
            color: Theme.text
            Layout.fillWidth: true
        }

        //? One row per active iface: name · ip, down/up rates
        Repeater {
            model: root.netRows
            delegate: RowLayout {
                id: ifaceRow
                required property var modelData
                Layout.fillWidth: true
                Label {
                    text: ifaceRow.modelData.name + " · " + (ifaceRow.modelData.ipv4 || ifaceRow.modelData.ipv6)
                    color: Theme.textFaint
                    font.pixelSize: 10
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    text: "↓ " + root.fmtRate(ifaceRow.modelData.downSpeed)
                    color: Theme.accentCpu
                    font.bold: true
                    font.pixelSize: 11
                }
                Label {
                    text: "↑ " + root.fmtRate(ifaceRow.modelData.upSpeed)
                    color: Theme.accentMem
                    font.bold: true
                    font.pixelSize: 11
                    Layout.alignment: Qt.AlignRight
                }
            }
        }

        Label {
            visible: root.netRows.length === 0
            text: qsTr("no active interfaces")
            color: Theme.textFaint
            font.pixelSize: 10
            Layout.fillWidth: true
        }

        HistoryGraph {
            Layout.fillWidth: true
            Layout.fillHeight: true
            samples: root.shownRow !== null ? root.shownRow.downHistory : []
            maxValue: root.shownRow !== null
                ? Math.max(10240, Math.max(root.shownRow.downSpeed, root.shownRow.upSpeed) * 1.3)
                : 10240
            lineColor: "#4fc3f7"
        }

        HistoryGraph {
            Layout.fillWidth: true
            Layout.fillHeight: true
            samples: root.shownRow !== null ? root.shownRow.upHistory : []
            maxValue: root.shownRow !== null
                ? Math.max(10240, Math.max(root.shownRow.downSpeed, root.shownRow.upSpeed) * 1.3)
                : 10240
            lineColor: "#81c784"
        }

        Label {
            text: root.shownRow !== null
                ? qsTr("total %1 ↓ · %2 ↑")
                    .arg(root.fmtBytes(root.shownRow.downTotal))
                    .arg(root.fmtBytes(root.shownRow.upTotal))
                : "—"
            color: Theme.textFaint
            font.pixelSize: 10
        }
    }
}
