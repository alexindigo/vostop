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

        Label {
            text: qsTr("Disks")
            font.bold: true
            font.pixelSize: 14
            color: "#e0e0e0"
        }

        //? Mount rows with usage bars + I/O column
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: DiskMonitor.mounts
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: mountRow
                required property var modelData
                required property int index
                width: ListView.view.width
                height: 34
                color: mountRow.index % 2 === 0 ? "#262626" : "#222222"
                radius: 4

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 4
                    spacing: 2

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: mountRow.modelData.name
                            color: "#dddddd"
                            font.pixelSize: 11
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.maximumWidth: 110
                        }
                        Label {
                            text: mountRow.modelData.fstype
                            color: "#666666"
                            font.pixelSize: 9
                        }
                        Item { Layout.fillWidth: true }
                        //? I/O column
                        Label {
                            text: mountRow.modelData.ioWrite > 0 ? "W " + root.fmtRate(mountRow.modelData.ioWrite) : ""
                            color: "#c9a227"
                            font.pixelSize: 9
                        }
                        Label {
                            text: mountRow.modelData.ioRead > 0 ? "R " + root.fmtRate(mountRow.modelData.ioRead) : ""
                            color: "#c9a227"
                            font.pixelSize: 9
                        }
                        Label {
                            text: mountRow.modelData.usedPercent + "%"
                            color: "#aaaaaa"
                            font.pixelSize: 10
                        }
                    }

                    //? Usage bar
                    ProgressBar {
                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        value: mountRow.modelData.usedPercent
                        indeterminate: false

                        background: Rectangle {
                            implicitHeight: 6
                            color: "#333333"
                            radius: 2
                        }
                        contentItem: Item {
                            implicitHeight: 6
                            Rectangle {
                                width: mountRow.modelData.usedPercent / 100 * parent.width
                                height: parent.height
                                radius: 2
                                color: mountRow.modelData.usedPercent > 90 ? "#e57373"
                                    : mountRow.modelData.usedPercent > 75 ? "#c9a227" : "#81c784"
                            }
                        }
                    }

                    Label {
                        text: root.fmtBytes(mountRow.modelData.used) + " / " + root.fmtBytes(mountRow.modelData.total)
                        color: "#888888"
                        font.pixelSize: 9
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: DiskMonitor.mounts.length === 0
                text: qsTr("no mounts")
                color: "#666666"
            }
        }

        //? I/O pressure sub-line (parity addition; hidden when PSI absent)
        Label {
            visible: DiskMonitor.ioPressureValid
            text: {
                if (!visible)
                    return ""
                let s = qsTr("io psi some %1%").arg(DiskMonitor.ioPressureSome[0].toFixed(1))
                if (DiskMonitor.ioPressureFull.length > 0 && DiskMonitor.ioPressureFull[0] > 0)
                    s += qsTr(" · full %1%").arg(DiskMonitor.ioPressureFull[0].toFixed(1))
                return s
            }
            color: "#c9a227"
            font.pixelSize: 10
        }
    }
}