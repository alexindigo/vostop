pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vostop

Rectangle {
    id: root

    //? Empty-state (phase 5): card hides when no supported GPU; shows an
    //? empty-state note instead when GPU data exists but is absent here.
    visible: opacity > 0
    color: Theme.cardBg
    border.color: Theme.cardBorder
    radius: 8

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("GPU")
                font.bold: true
                font.pixelSize: 14
                color: Theme.text
            }
            Item { Layout.fillWidth: true }
        }

        Label {
            visible: GpuMonitor.gpus.length === 0
            text: qsTr("no supported GPU detected")
            color: Theme.textGhost
            font.pixelSize: 11
        }

        Repeater {
            model: GpuMonitor.gpus

            ColumnLayout {
                id: gpuDev
                required property var modelData
                required property int index
                Layout.fillWidth: true
                spacing: 3

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: gpuDev.modelData.name
                        color: Theme.textBright
                        font.pixelSize: 11
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.maximumWidth: 200
                    }
                    //? Honest label required for the fdinfo-aggregate device card
                    Label {
                        visible: gpuDev.modelData.approximate
                        text: qsTr("approximate")
                        color: Theme.accentWarn
                        font.pixelSize: 9
                        font.italic: true
                    }
                    Item { Layout.fillWidth: true }
                    Label {
                        visible: gpuDev.modelData.util >= 0
                        text: gpuDev.modelData.util.toFixed(0) + "%"
                        color: Theme.accentGpu
                        font.bold: true
                        font.pixelSize: 15
                    }
                    Label {
                        visible: gpuDev.modelData.temp > 0
                        text: gpuDev.modelData.temp + "°C"
                        color: Theme.textDim
                        font.pixelSize: 10
                    }
                    Label {
                        visible: gpuDev.modelData.powerMw > 0
                        text: (gpuDev.modelData.powerMw / 1000).toFixed(1) + " W"
                        color: Theme.textDim
                        font.pixelSize: 10
                    }
                    Label {
                        visible: gpuDev.modelData.clockMhz > 0
                        text: gpuDev.modelData.clockMhz + " MHz"
                        color: Theme.textFaint
                        font.pixelSize: 10
                    }
                }

                ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    value: Math.max(0, gpuDev.modelData.util)

                    background: Rectangle {
                        implicitHeight: 8
                        color: Theme.innerBg
                        radius: 3
                    }
                    contentItem: Item {
                        implicitHeight: 8
                        Rectangle {
                            width: Math.max(0, gpuDev.modelData.util) / 100 * parent.width
                            height: parent.height
                            radius: 3
                            color: Theme.accentGpu
                        }
                    }
                }

                Label {
                    visible: gpuDev.modelData.memTotal > 0
                    text: qsTr("vram %1 / %2")
                        .arg(root.fmtBytes(gpuDev.modelData.memUsed))
                        .arg(root.fmtBytes(gpuDev.modelData.memTotal))
                    color: Theme.textDim
                    font.pixelSize: 10
                }
            }
        }

        Item { Layout.fillHeight: true }
    }

    function fmtBytes(b) {
        if (b >= 1073741824) return (b / 1073741824).toFixed(1) + " GiB"
        if (b >= 1048576) return (b / 1048576).toFixed(0) + " MiB"
        if (b >= 1024) return (b / 1024).toFixed(0) + " KiB"
        return b + " B"
    }
}