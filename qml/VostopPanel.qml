pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vostop

//? Panel wrapper: file-Loader for one panel + error tile on load/creation
//? failure (spec §3 — tiles cover load/creation failure; post-load exceptions
//? surface via the engine log). panelTitle passthrough is unused
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

    //? One Loader path for user panels and built-ins: PanelLayoutBackend
    //? resolves user dir first (shadows built-ins), then the CMake-baked dir
    readonly property string resolvedPath: root.sourceName === ""
        ? "" : PanelLayoutBackend.resolveSource(root.sourceName)

    Loader {
        id: panelLoader
        anchors.fill: parent
        anchors.margins: root.panelGap
        asynchronous: true
        source: root.resolvedPath === "" ? "" : "file://" + root.resolvedPath
    }

    //? Error tile: covers resolution failure + load/creation failure
    Rectangle {
        anchors.fill: parent
        anchors.margins: root.panelGap
        visible: root.resolvedPath === "" || panelLoader.status === Loader.Error
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
                visible: root.resolvedPath === ""
                text: qsTr("not found in user or built-in panels")
                color: Theme.textFaint
                font.pixelSize: 9
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                visible: root.resolvedPath !== ""
                text: root.resolvedPath
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
