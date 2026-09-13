pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vostop

//? Recursive layout renderer over PanelLayoutBackend.tree (v1 view mode).
//? Containers become RowLayout (direction "rows", horizontal) / ColumnLayout
//? ("cols", vertical) whose ONLY gap mechanism is spacing between siblings —
//? edge insets belong to the shell (single ownership per gap); leaves become
//? VostopPanel. grow splits extra space along the container axis (weight =
//? node's grow), cross-axis always fills; minWidth/minHeight floor the box.
Item {
    id: root

    property var tree: PanelLayoutBackend.tree

    //? Recursive node component — instantiated through Loaders (inline
    //? component, so no re-entrant document load). Each Loader hosts one
    //? node and carries that node's layout role: along-axis extra space
    //? splits by grow, cross-axis fills, minimums floor the box.
    Component {
        id: nodeComponent

        Item {
            id: wrapper

            //? Hosting Loader (set by parent before creation)
            property var host: parent
            property var node: wrapper.host ? wrapper.host.nodeData : null
            property string parentDir: wrapper.host ? wrapper.host.dir : ""

            property int nodeGrow: wrapper.node && wrapper.node.grow !== undefined ? wrapper.node.grow : 1
            property int nodeGap: wrapper.node && wrapper.node.gap !== undefined ? wrapper.node.gap : 0
            property int nodeMinW: wrapper.node && wrapper.node.minWidth !== undefined ? wrapper.node.minWidth : 0
            property int nodeMinH: wrapper.node && wrapper.node.minHeight !== undefined ? wrapper.node.minHeight : 0
            property string nodeDir: wrapper.node && wrapper.node.direction !== undefined ? wrapper.node.direction : "rows"
            property bool isRoot: wrapper.parentDir === ""
            property bool isInvalid: wrapper.node ? wrapper.node._invalid === true : false
            property bool isEmpty: wrapper.node ? wrapper.node._empty === true : false
            property bool isContainer: wrapper.node ? (wrapper.node.children !== undefined && !wrapper.isInvalid) : false
            property bool isLeaf: wrapper.node ? (wrapper.node.source !== undefined && !wrapper.isInvalid) : false

            anchors.fill: parent

            //? Content size propagates up the tree so the shell can floor the
            //? window at the content's implicit size (never eats content)
            implicitWidth: rowsLayout.visible ? rowsLayout.implicitWidth
                : colsLayout.visible ? colsLayout.implicitWidth
                : leafPanel.implicitWidth
            implicitHeight: rowsLayout.visible ? rowsLayout.implicitHeight
                : colsLayout.visible ? colsLayout.implicitHeight
                : leafPanel.implicitHeight

            //? Invalid node → error tile
            Rectangle {
                visible: wrapper.isInvalid
                anchors.fill: parent
                color: Theme.cardBg
                border.color: Theme.cardBorder
                radius: 8

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 4

                    Label {
                        text: qsTr("invalid layout node")
                        color: Theme.accentDanger
                        font.bold: true
                        font.pixelSize: 11
                        Layout.fillWidth: true
                    }
                    Label {
                        text: wrapper.node && wrapper.node._error ? wrapper.node._error : qsTr("unknown error")
                        color: Theme.textFaint
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Item { Layout.fillHeight: true }
                }
            }

            //? Leaf → VostopPanel (gap pads the widget on all sides, spec §2)
            VostopPanel {
                id: leafPanel
                visible: wrapper.isLeaf
                anchors.fill: parent
                sourceName: wrapper.isLeaf ? wrapper.node.source : ""
                panelGap: wrapper.nodeGap
            }

            //? Container along "rows" → horizontal; spacing between children
            //? only, no edge insets (the shell owns the frame), cross-axis fills
            RowLayout {
                id: rowsLayout
                visible: wrapper.isContainer && wrapper.nodeDir === "rows"
                anchors.fill: parent
                spacing: wrapper.nodeGap

                Repeater {
                    model: wrapper.isContainer && wrapper.nodeDir === "rows" ? wrapper.node.children : []
                    delegate: Loader {
                        id: rowCell

                        required property var modelData
                        required property int index

                        //? Loader carries the layout role; the hosted node renders inside
                        property var nodeData: rowCell.modelData
                        property string dir: "rows"

                        Layout.fillWidth: rowCell.modelData ? rowCell.modelData.grow > 0 : false
                        Layout.fillHeight: true
                        Layout.preferredWidth: rowCell.modelData && rowCell.modelData.grow > 0 ? rowCell.modelData.grow * 100 : 0
                        Layout.minimumWidth: rowCell.modelData && rowCell.modelData.minWidth !== undefined ? rowCell.modelData.minWidth : 0
                        Layout.minimumHeight: rowCell.modelData && rowCell.modelData.minHeight !== undefined ? rowCell.modelData.minHeight : 0

                        sourceComponent: nodeComponent
                    }
                }
            }

            //? Container along "cols" → vertical; same single mechanism
            ColumnLayout {
                id: colsLayout
                visible: wrapper.isContainer && wrapper.nodeDir === "cols"
                anchors.fill: parent
                spacing: wrapper.nodeGap

                Repeater {
                    model: wrapper.isContainer && wrapper.nodeDir === "cols" ? wrapper.node.children : []
                    delegate: Loader {
                        id: colCell

                        required property var modelData
                        required property int index

                        property var nodeData: colCell.modelData
                        property string dir: "cols"

                        Layout.fillWidth: true
                        Layout.fillHeight: colCell.modelData ? colCell.modelData.grow > 0 : false
                        Layout.preferredHeight: colCell.modelData && colCell.modelData.grow > 0 ? colCell.modelData.grow * 100 : 0
                        Layout.minimumWidth: colCell.modelData && colCell.modelData.minWidth !== undefined ? colCell.modelData.minWidth : 0
                        Layout.minimumHeight: colCell.modelData && colCell.modelData.minHeight !== undefined ? colCell.modelData.minHeight : 0

                        sourceComponent: nodeComponent
                    }
                }
            }
        }
    }

    //? Root node fills this item; the binding chain re-renders on tree change
    Loader {
        id: rootLoader
        anchors.fill: parent
        property var nodeData: root.tree
        property string dir: ""
        sourceComponent: nodeComponent
    }

    //? Content size up to the shell (window minimum = content + frame inset)
    implicitWidth: rootLoader.implicitWidth
    implicitHeight: rootLoader.implicitHeight
}
