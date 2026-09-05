import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vostop

Rectangle {
    id: root

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
                text: qsTr("Temps & Power")
                font.bold: true
                font.pixelSize: 14
                color: Theme.text
            }
            Item { Layout.fillWidth: true }
            //? RAPL package watts (when probed)
            Label {
                visible: SensorsMonitor.cpuWattsAvailable
                text: SensorsMonitor.cpuWatts.toFixed(1) + " W"
                color: Theme.accentHot
                font.bold: true
                font.pixelSize: 14
            }
        }

        Label {
            visible: !SensorsMonitor.sensorsAvailable
            text: qsTr("no sensors found")
            color: Theme.textGhost
            font.pixelSize: 11
        }

        Label {
            visible: SensorsMonitor.sensorsAvailable
            text: qsTr("cpu %1 °C · %2")
                .arg(SensorsMonitor.cpuTemp)
                .arg(SensorsMonitor.cpuSensorName)
            color: Theme.accentHot
            font.bold: true
            font.pixelSize: 12
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        //? Per-core temps (hidden when cpu_temp_only)
        GridLayout {
            visible: SensorsMonitor.coreTemps.length > 0
            Layout.fillWidth: true
            columns: 4
            columnSpacing: 6
            rowSpacing: 3

            Repeater {
                model: SensorsMonitor.coreTemps.length

                Label {
                    required property int index
                    text: index + ": " + SensorsMonitor.coreTemps[index] + "°"
                    color: SensorsMonitor.coreTemps[index] >= 80 ? "Theme.accentDanger"
                        : SensorsMonitor.coreTemps[index] >= 60 ? Theme.accentWarn : "#aaaaaa"
                    font.pixelSize: 9
                }
            }
        }

        Item { Layout.fillWidth: true }

        //? Battery
        ColumnLayout {
            visible: SensorsMonitor.batteryAvailable
            Layout.fillWidth: true
            spacing: 2

            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: qsTr("battery %1%").arg(SensorsMonitor.batteryPct)
                    color: Theme.accentMem
                    font.bold: true
                    font.pixelSize: 12
                }
                Item { Layout.fillWidth: true }
                Label {
                    visible: SensorsMonitor.batteryWatts >= 0
                    text: SensorsMonitor.batteryWatts.toFixed(1) + " W"
                    color: Theme.textDim
                    font.pixelSize: 10
                }
            }

            ProgressBar {
                Layout.fillWidth: true
                from: 0
                to: 100
                value: SensorsMonitor.batteryPct

                background: Rectangle {
                    implicitHeight: 7
                    color: Theme.innerBg
                    radius: 3
                }
                contentItem: Item {
                    implicitHeight: 7
                    Rectangle {
                        width: SensorsMonitor.batteryPct / 100 * parent.width
                        height: parent.height
                        radius: 3
                        color: SensorsMonitor.batteryPct < 20 ? "Theme.accentDanger" : "#81c784"
                    }
                }
            }

            Label {
                text: qsTr("%1").arg(SensorsMonitor.batteryStatus)
                    + (SensorsMonitor.batterySeconds > 0
                        ? qsTr(" · %1h %2m").arg(Math.floor(SensorsMonitor.batterySeconds / 3600)).arg(Math.floor((SensorsMonitor.batterySeconds % 3600) / 60))
                        : "")
                color: Theme.textFaint
                font.pixelSize: 10
            }
        }

        Item { Layout.fillHeight: true }
    }
}