import QtQuick
import org.kde.kirigami as Kirigami

Kirigami.Page {
  id: page
  title: "Directory"

  // A control character separates paths so every filename stays intact.
  property string set: ""
  readonly property var selectedPaths: page.set.length > 0
      ? page.set.split(String.fromCharCode(31)) : []

  function toggleSelection(path) {
    const marker = String.fromCharCode(31);
    var parts = page.set.length > 0 ? page.set.split(marker) : [];
    const index = parts.indexOf(path);
    if (index >= 0)
      parts.splice(index, 1);
    else
      parts.push(path);
    page.set = parts.join(marker);
  }

  function clearSelection() {
    page.set = "";
  }

  ListView {
    anchors.fill: parent
    model: hermes.directory

    delegate: Item {
      width: ListView.view.width
      height: Kirigami.Units.gridUnit * 2
      property bool selected: page.selectedPaths.indexOf(model.path) >= 0

      Rectangle {
        anchors.fill: parent
        color: Kirigami.Theme.highlightColor
        opacity: 0.25
        visible: parent.selected
      }

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
        onClicked: page.toggleSelection(model.path)
        onDoubleClicked: hermes.openEntry(model.path)
      }
    }
  }
}
