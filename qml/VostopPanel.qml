pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vostop

//? Panel wrapper: file-Loader for one panel + error tile on load/creation
//? failure (spec §3 — never a crash). panelTitle passthrough is unused
//? visually in v1 view mode (edit-mode chrome later).
Item {
    id: root
    required property string sourceName
    property int panelGap: 0

    //? Loaded panel item (var so qmllint stays quiet about optional
    //? panelTitle on user panels — the contract makes it optional)
    readonly property var panelItem: panelLoader.status === Loader.Ready ? panelLoader.item : null
    readonly property string panelTitle: root.panelItem && root.panelItem.panelTitle !== undefined
        ? String(root.panelItem.panelTitle) : ""

    Loader {
        id: panelLoader
        anchors.fill: parent
        anchors.margins: root.panelGap
        asynchronous: true
        source: "file://" + PanelLayoutBackend.builtInDir + "/" + root.sourceName + ".qml"
    }

    //? Error tile: covers load/creation failure
    Rectangle {
        anchors.fill: parent
        anchors.margins: root.panelGap
        visible: panelLoader.status === Loader.Error || panelLoader.source.toString() === ""
        color: Theme.cardBg
        border.color: Theme.cardBorder
        radius: 8

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 4

            Label {
                text: qsTr("panel failed: %1").arg(root.sourceName)
                color: Theme.accentDanger
                font.bold: true
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                text: panelLoader.source.toString()
                color: Theme.textFaint
                font.pixelSize: 9
                wrapMode: Text.WordWrap
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Item { Layout.fillHeight: true }
        }
    }
}
