import QtQuick
import org.kde.kirigami as Kirigami

// The controller computes every leaf's rectangle; this item only places it.
// Geometry is animated as it changes, but real content is never held back for
// the animation to finish.
Item {
  id: paneRoot
  property int paneId: -1
  property var directoryModel: null
  property bool focused: false
  property int motionDuration: 180
  property alias directory: directoryPane
  property alias openAnimation: openAnimation

  objectName: "paneView_" + paneId
  clip: true

  DirectoryPage {
    id: directoryPane
    anchors.fill: parent
    paneId: paneRoot.paneId
    model: paneRoot.directoryModel
    focused: paneRoot.focused
  }

  // Drawn above the page background so the focus border is not covered.
  Rectangle {
    anchors.fill: parent
    z: 5
    color: "transparent"
    border.color: paneRoot.focused ? Kirigami.Theme.highlightColor : "transparent"
    border.width: paneRoot.focused ? 2 : 0
  }

  // Clicking anywhere in a pane focuses it; composed events still reach the
  // directory view underneath, so selecting a row also sets the focus.
  MouseArea {
    anchors.fill: parent
    z: 10
    propagateComposedEvents: true
    onClicked: function(mouse) {
      if (!paneRoot.focused)
        hermes.setFocusedPane(paneRoot.paneId)
      mouse.accepted = false
    }
    onDoubleClicked: function(mouse) { mouse.accepted = false }
  }

  Behavior on x {
    NumberAnimation {
      duration: paneRoot.motionDuration
      easing.type: Easing.OutQuad
    }
  }
  Behavior on y {
    NumberAnimation {
      duration: paneRoot.motionDuration
      easing.type: Easing.OutQuad
    }
  }
  Behavior on width {
    NumberAnimation {
      duration: paneRoot.motionDuration
      easing.type: Easing.OutQuad
    }
  }
  Behavior on height {
    NumberAnimation {
      duration: paneRoot.motionDuration
      easing.type: Easing.OutQuad
    }
  }

  NumberAnimation {
    id: openAnimation
    objectName: "paneOpenAnimation"
    target: paneRoot
    property: "opacity"
    from: 0
    to: 1
    duration: paneRoot.motionDuration
    easing.type: Easing.OutQuad
    running: true
  }
}
