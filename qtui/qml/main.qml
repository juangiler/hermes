import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
  id: root
  title: "Hermes"

  globalDrawer: Kirigami.GlobalDrawer {
    title: "Places"
    titleIcon: "folder"
    modal: true

    ListView {
      width: parent.width
      height: Math.min(contentHeight, root.height / 2)
      clip: true
      model: hermes.places
      delegate: ItemDelegate {
        width: ListView.view.width
        text: model.label
        onClicked: {
          hermes.navigate(model.path)
          root.globalDrawer.close()
        }
      }
    }
  }

  pageStack.initialPage: DirectoryPage {
    header: Column {
      width: parent.width

      TabBar {
        width: parent.width
        currentIndex: hermes.currentTab
        Repeater {
          model: hermes.tabs
          TabButton {
            text: modelData
            onClicked: hermes.selectTab(index)
          }
        }
      }

      ToolBar {
        width: parent.width
        Row {
          spacing: Kirigami.Units.smallSpacing
          anchors.verticalCenter: parent.verticalCenter
          ToolButton {
            text: "Up"
            onClicked: hermes.up()
          }
          ToolButton {
            text: "Back"
            enabled: hermes.canGoBack
            onClicked: hermes.back()
          }
          ToolButton {
            text: "Forward"
            enabled: hermes.canGoForward
            onClicked: hermes.forward()
          }
        }
      }
    }
  }
}
