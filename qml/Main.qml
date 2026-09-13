pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Vostop

ApplicationWindow {
    id: root
    width: 1400
    height: 950
    //? Never let the window eat content: the minimum is the layout's own
    //? implicit size plus the shell's frame inset
    minimumWidth: panelGrid.implicitWidth + 16
    minimumHeight: panelGrid.implicitHeight + 16
    visible: true
    title: "vostop"
    color: Theme.windowBg

    //? The shell owns the window frame inset — the single outer margin; the
    //? panel engine owns only spacing between siblings (gap single-ownership)
    PanelLayout {
        id: panelGrid
        anchors.fill: parent
        anchors.margins: 8
    }
}
