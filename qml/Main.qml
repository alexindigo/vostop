pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vostop

ApplicationWindow {
    id: root
    width: 1400
    height: 950
    minimumWidth: 900
    minimumHeight: 600
    visible: true
    title: "vostop"
    color: Theme.windowBg

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        //? Header: title + poll interval + theme + About (phase 6)
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 8

            Label {
                text: qsTr("vostop")
                font.bold: true
                font.pixelSize: 16
                color: Theme.text
            }
            Item { Layout.fillWidth: true }

            ComboBox {
                id: intervalPicker
                font.pixelSize: 11
                model: [250, 500, 1000, 2000]
                displayText: qsTr("%1 ms").arg(Settings.pollIntervalMs)
                onActivated: function (idx) { Settings.pollIntervalMs = model[idx] }
                Component.onCompleted: {
                    const i = model.indexOf(Settings.pollIntervalMs)
                    currentIndex = i >= 0 ? i : 2
                }
            }

            Button {
                checkable: true
                checked: Settings.theme !== "light"
                text: checked ? qsTr("dark") : qsTr("light")
                font.pixelSize: 11
                onCheckedChanged: Settings.theme = checked ? "dark" : "light"
            }

            Button {
                text: qsTr("About")
                font.pixelSize: 11
                flat: true
                onClicked: aboutDialog.open()
            }
        }

        PanelLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 8
            Layout.topMargin: 0
        }
    }

    //? About dialog with btop attribution (phase 6; plan.md §9 Q4)
    Dialog {
        id: aboutDialog
        title: qsTr("About vostop")
        modal: true
        standardButtons: Dialog.Close
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 420

        ColumnLayout {
            width: parent.width
            spacing: 8
            Label {
                text: qsTr("vostop — Linux task manager")
                font.bold: true
                color: Theme.text
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Hardware/process collection derived from btop (https://github.com/aristocratos/btop), © 2021 Aristocratos, licensed under the Apache License 2.0. See THIRD-PARTY-NOTICES for details.")
                color: Theme.textFaint
                font.pixelSize: 11
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("vostop itself is licensed GPLv3-or-later.")
                color: Theme.textDim
                font.pixelSize: 11
            }
        }
    }
}