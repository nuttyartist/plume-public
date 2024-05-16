import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12

ScrollBar {
    id: scrollBarControl
    visible: active
//    parent: scrollViewControl
//    x: parent.mirrored ? 0 : parent.width - width
    y: parent.mirrored ? 0 : parent.height - height
    width: parent.availableWidth
    property var themeData: {{theme: "Light"}}
    property bool isDarkGray: true // Deterimnes wether the scrollbar color in Dark mode should be grayish or darkish
    property bool showBackground: false

    onActiveFocusChanged: {
        console.log("active:", scrollBarControl.active);
    }

    background: Rectangle {
        width: scrollBarControl.availableWidth
        radius: height / 2
        height: scrollBarControl.hovered ? 1 : 7
        color: "transparent"
        border.color: scrollBarControl.themeData.theme === "Dark" ? "#534c53" : "#dfdfdf"
        opacity: 0.25
        visible: scrollBarControl.active && scrollBarControl.hovered && scrollBarControl.showBackground
    }

    contentItem: Rectangle {
        implicitHeight: scrollBarControl.hovered ? 12 : 7
        radius: height / 2
        color: scrollBarControl.pressed ? (scrollBarControl.themeData.theme === "Dark" ? (scrollBarControl.isDarkGray ? "#858185" : "#040404") :"#5e5d5d") : (scrollBarControl.themeData.theme === "Dark" ? (scrollBarControl.isDarkGray ? "#a29ea2" : "#0e0e0e") : "#7d7d7d")
        opacity: scrollBarControl.policy === ScrollBar.AlwaysOn || (scrollBarControl.active && scrollBarControl.size < 1.0) ? 0.75 : 0

        Behavior on opacity {
            enabled: opacityDelayTimer.running // Sync the animation's running state with the timer's
            NumberAnimation {
                duration: 250
            }
        }

        Timer {
            id: opacityDelayTimer
            interval: 1200
            repeat: false

            onTriggered: {
                parent.opacity = scrollBarControl.policy === ScrollBar.AlwaysOn || (scrollBarControl.active && scrollBarControl.size < 1.0) ? 0.75 : 0
            }
        }

        onOpacityChanged: {
            if (!opacityDelayTimer.running) {
                opacityDelayTimer.start()
            }
        }
    }

    function keepActive () {
        scrollBarControl.active = true;
        loseActiveTimer.start();
    }

    Timer {
        id: loseActiveTimer
        interval: 2000

        onTriggered: {
            scrollBarControl.active = false;
        }
    }
}

