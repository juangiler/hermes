import QtQuick
import org.kde.kirigami as Kirigami

Kirigami.Page {
  id: page
  objectName: "directoryPage"
  title: "Directory"

  property var selectedPaths: []
  // Zoom scales rows and the fixed columns; the name column absorbs the rest.
  property real rowHeight: Kirigami.Units.gridUnit * 2 * hermes.zoom
  property real fixedColumnWidth: {
    var total = 0;
    for (var i = 0; i < hermes.columns.length; ++i) {
      var column = hermes.columns[i];
      if (column.key !== "name")
        total += column.width * hermes.zoom;
    }
    return total;
  }

  function toggleSelection(path) {
    var parts = page.selectedPaths.slice();
    const index = parts.indexOf(path);
    if (index >= 0)
      parts.splice(index, 1);
    else
      parts.push(path);
    page.selectedPaths = parts;
  }

  function clearSelection() {
    page.selectedPaths = [];
  }

  // -1 means the recursive folder size is still being computed; never show it
  // as a zero-byte file. Match the legacy unit formatting for known sizes.
  function sizeText(bytes) {
    if (bytes < 0)
      return "...";
    if (bytes < 1024)
      return bytes + " B";
    var units = ["KB", "MB", "GB", "TB", "PB"];
    var value = bytes / 1024;
    var unit = 0;
    while (value >= 1024 && unit < units.length - 1) {
      value /= 1024;
      ++unit;
    }
    return (value < 10 ? value.toFixed(1) : Math.round(value)) + " " + units[unit];
  }

  // Same information as the legacy column, expressed in the user's locale.
  function dateText(seconds) {
    if (!seconds)
      return "";
    var locale = Qt.locale();
    return locale.toString(new Date(seconds * 1000),
                           locale.dateFormat(Locale.ShortFormat) + " "
                           + locale.timeFormat(Locale.ShortFormat));
  }

  Connections {
    target: hermes.directory
    function onPathChanged() {
      page.clearSelection()
    }
  }

  Connections {
    target: hermes
    function onCurrentTabChanged() {
      page.clearSelection()
    }
  }

  // List mode: one line per entry with an icon, name and size.
  ListView {
    objectName: "listView"
    anchors.fill: parent
    visible: hermes.viewMode === 0
    model: hermes.directory
    clip: true

    delegate: Item {
      width: ListView.view.width
      height: page.rowHeight
      property bool selected: page.selectedPaths.indexOf(model.path) >= 0

      Rectangle {
        anchors.fill: parent
        color: Kirigami.Theme.highlightColor
        opacity: 0.25
        visible: parent.selected
      }

      Kirigami.Icon {
        id: listIcon
        source: model.isDir ? "folder" : "text-plain"
        width: Kirigami.Units.iconSizes.small * hermes.zoom
        height: width
        anchors.left: parent.left
        anchors.leftMargin: Kirigami.Units.smallSpacing
        anchors.verticalCenter: parent.verticalCenter
      }

      Text {
        anchors.left: listIcon.right
        anchors.leftMargin: Kirigami.Units.smallSpacing
        anchors.right: listSize.left
        anchors.verticalCenter: parent.verticalCenter
        text: model.name
        elide: Text.ElideRight
        color: Kirigami.Theme.textColor
      }

      Text {
        id: listSize
        anchors.right: parent.right
        anchors.rightMargin: Kirigami.Units.smallSpacing
        anchors.verticalCenter: parent.verticalCenter
        text: page.sizeText(model.size)
        color: Kirigami.Theme.textColor
      }

      MouseArea {
        anchors.fill: parent
        onClicked: page.toggleSelection(model.path)
        onDoubleClicked: hermes.openEntry(model.path)
      }
    }
  }

  // Details mode: one Text per configured column, laid out by a Repeater.
  ListView {
    objectName: "detailsView"
    anchors.fill: parent
    visible: hermes.viewMode === 1
    model: hermes.directory
    clip: true

    delegate: Item {
      width: ListView.view.width
      height: page.rowHeight
      property string entryName: model.name
      property string entrySize: page.sizeText(model.size)
      property string entryDate: page.dateText(model.modified)
      property bool selected: page.selectedPaths.indexOf(model.path) >= 0

      Rectangle {
        anchors.fill: parent
        color: Kirigami.Theme.highlightColor
        opacity: 0.25
        visible: parent.selected
      }

      Row {
        anchors.fill: parent
        Repeater {
          model: hermes.columns
          delegate: Text {
            width: modelData.key === "name"
                ? Math.max(1, parent.width - page.fixedColumnWidth)
                : modelData.width * hermes.zoom
            height: parent.height
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            color: Kirigami.Theme.textColor
            text: modelData.key === "name" ? entryName
                : modelData.key === "date" ? entryDate
                : modelData.key === "size" ? entrySize
                : ""
          }
        }
      }

      MouseArea {
        anchors.fill: parent
        onClicked: page.toggleSelection(model.path)
        onDoubleClicked: hermes.openEntry(model.path)
      }
    }
  }

  // Grid mode: tiles with a large icon, the name and the size.
  GridView {
    objectName: "gridView"
    anchors.fill: parent
    visible: hermes.viewMode === 2
    model: hermes.directory
    cellWidth: Kirigami.Units.gridUnit * 9 * hermes.zoom
    cellHeight: Kirigami.Units.gridUnit * 7 * hermes.zoom
    clip: true

    delegate: Item {
      width: GridView.view.cellWidth
      height: GridView.view.cellHeight
      property bool selected: page.selectedPaths.indexOf(model.path) >= 0

      Rectangle {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing
        color: Kirigami.Theme.highlightColor
        opacity: 0.25
        visible: parent.selected
      }

      Kirigami.Icon {
        id: gridIcon
        source: model.isDir ? "folder" : "text-plain"
        width: Kirigami.Units.iconSizes.large * hermes.zoom
        height: width
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Kirigami.Units.smallSpacing
      }

      Text {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: gridIcon.bottom
        anchors.topMargin: Kirigami.Units.smallSpacing
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        text: model.name
        color: Kirigami.Theme.textColor
      }

      Text {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Kirigami.Units.smallSpacing
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        text: page.sizeText(model.size)
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
