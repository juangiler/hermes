import QtQuick
import org.kde.kirigami as Kirigami

Kirigami.Page {
  title: "Directory"

  ListView {
    anchors.fill: parent
    model: hermes.directory

    delegate: Item {
      width: ListView.view.width
      height: Kirigami.Units.gridUnit * 2

      Kirigami.Icon {
        id: entryIcon
        source: model.isDir ? "folder" : "text-plain"
        width: Kirigami.Units.iconSizes.small
        height: width
        anchors.left: parent.left
        anchors.leftMargin: Kirigami.Units.smallSpacing
        anchors.verticalCenter: parent.verticalCenter
      }

      Text {
        anchors.left: entryIcon.right
        anchors.leftMargin: Kirigami.Units.smallSpacing
        anchors.verticalCenter: parent.verticalCenter
        text: model.name
        color: Kirigami.Theme.textColor
      }

      Text {
        anchors.right: parent.right
        anchors.rightMargin: Kirigami.Units.smallSpacing
        anchors.verticalCenter: parent.verticalCenter
        text: model.size
        color: Kirigami.Theme.textColor
      }

      MouseArea {
        anchors.fill: parent
        onDoubleClicked: {
          if (hermes.openEntry)
            hermes.openEntry(model.path)
        }
      }
    }
  }
}
