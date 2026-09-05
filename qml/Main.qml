import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vostop

ApplicationWindow {
    id: root
    width: 1200
    height: 800
    visible: true
    title: "vostop"

    GridLayout {
        anchors.fill: parent
        anchors.margins: 8
        columns: 3
        rowSpacing: 8
        columnSpacing: 8

        CpuCard { Layout.fillWidth: true; Layout.fillHeight: true }
        MemCard { Layout.fillWidth: true; Layout.fillHeight: true }
        DiskCard { Layout.fillWidth: true; Layout.fillHeight: true }
        NetCard { Layout.fillWidth: true; Layout.fillHeight: true }
        GpuCard { Layout.fillWidth: true; Layout.fillHeight: true }
        TempCard { Layout.fillWidth: true; Layout.fillHeight: true }
        ProcessList {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.columnSpan: 3
            Layout.minimumHeight: 320
        }
    }
}