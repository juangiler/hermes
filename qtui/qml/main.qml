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
    id: directoryPage
    header: Column {
      width: parent.width

      Row {
        objectName: "tabBar"
        width: parent.width
        spacing: Kirigami.Units.smallSpacing
        Repeater {
          objectName: "tabRepeater"
          model: hermes.tabs.length
          delegate: Component {
            ToolButton {
              id: tabButton
              objectName: "tabLabel_" + index
              property string tabPath: hermes.tabs[index]
              checkable: true
              checked: index === hermes.currentTab
              text: {
                var components = tabPath.split("/");
                var label = "";
                for (var i = 0; i < components.length; ++i) {
                  if (components[i].length > 0)
                    label = components[i];
                }
                return label.length > 0 ? label : "/";
              }
              onClicked: hermes.selectTab(index)

              contentItem: Row {
                spacing: Kirigami.Units.smallSpacing
                Text {
                  anchors.verticalCenter: parent.verticalCenter
                  text: tabButton.text
                  color: Kirigami.Theme.textColor
                }
                ToolButton {
                  objectName: "closeTabButton_" + index
                  icon.name: "window-close"
                  onClicked: hermes.closeTab(index)
                }
              }
            }
          }
        }
      }

      ToolBar {
        width: parent.width
        Column {
          width: parent.width
          spacing: Kirigami.Units.smallSpacing
          anchors.verticalCenter: parent.verticalCenter
          Row {
            spacing: Kirigami.Units.smallSpacing
            ToolButton {
              objectName: "newTabButton"
              text: "New tab"
              onClicked: hermes.newTab(hermes.tabs[hermes.currentTab])
            }
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
            ToolButton {
              action: Kirigami.Action {
                text: "New folder"
                icon.name: "folder-new"
                onTriggered: newFolderDialog.open()
              }
            }
            ToolButton {
              action: Kirigami.Action {
                text: "Rename"
                icon.name: "edit-rename"
                enabled: directoryPage.selectedPaths.length === 1
                onTriggered: {
                  const current = directoryPage.selectedPaths[0];
                  renameName.text = current.substring(current.lastIndexOf("/") + 1);
                  renameDialog.open();
                }
              }
            }
            ToolButton {
              action: Kirigami.Action {
                text: "Copy"
                icon.name: "edit-copy"
                enabled: directoryPage.selectedPaths.length > 0
                onTriggered: {
                  hermes.copySelection(directoryPage.selectedPaths, false);
                  directoryPage.clearSelection();
                }
              }
            }
            ToolButton {
              action: Kirigami.Action {
                text: "Cut"
                icon.name: "edit-cut"
                enabled: directoryPage.selectedPaths.length > 0
                onTriggered: {
                  hermes.copySelection(directoryPage.selectedPaths, true);
                  directoryPage.clearSelection();
                }
              }
            }
            ToolButton {
              objectName: "pasteButton"
              action: Kirigami.Action {
                text: "Paste"
                icon.name: "edit-paste"
                enabled: hermes.canPaste
                onTriggered: hermes.pasteIntoCurrentDirectory()
              }
            }
            ToolButton {
              action: Kirigami.Action {
                text: "Trash"
                icon.name: "user-trash"
                enabled: directoryPage.selectedPaths.length > 0
                onTriggered: {
                  hermes.removeEntries(directoryPage.selectedPaths, true);
                }
              }
            }
            ToolButton {
              objectName: "deleteButton"
              action: Kirigami.Action {
                text: "Delete"
                icon.name: "edit-delete"
                enabled: directoryPage.selectedPaths.length > 0
                onTriggered: {
                  deleteConfirmDialog.open();
                }
              }
            }
          }
          Row {
            spacing: Kirigami.Units.smallSpacing
            ToolButton {
              objectName: "listViewButton"
              text: "List"
              checkable: true
              checked: hermes.viewMode === 0
              onClicked: hermes.setViewMode(0)
            }
            ToolButton {
              objectName: "detailsViewButton"
              text: "Details"
              checkable: true
              checked: hermes.viewMode === 1
              onClicked: hermes.setViewMode(1)
            }
            ToolButton {
              objectName: "gridViewButton"
              text: "Grid"
              checkable: true
              checked: hermes.viewMode === 2
              onClicked: hermes.setViewMode(2)
            }
            ToolButton {
              objectName: "showHiddenButton"
              text: "Hidden"
              checkable: true
              checked: hermes.showHidden
              onToggled: hermes.setShowHidden(checked)
            }
            ToolButton {
              objectName: "folderSizesButton"
              text: "Folder sizes"
              checkable: true
              checked: hermes.folderSizes
              onToggled: hermes.setFolderSizes(checked)
            }
            Repeater {
              model: hermes.directory.sortKeys()
              delegate: ToolButton {
                objectName: "sortButton_" + modelData
                text: modelData
                checkable: true
                checked: hermes.sortKey === modelData
                onClicked: hermes.setSortKey(modelData)
              }
            }
            ToolButton {
              objectName: "zoomOutButton"
              text: "-"
              onClicked: hermes.setZoom(hermes.zoom - 0.2)
            }
            ToolButton {
              objectName: "zoomInButton"
              text: "+"
              onClicked: hermes.setZoom(hermes.zoom + 0.2)
            }
          }
        }
      }
    }

    Kirigami.Dialog {
      id: newFolderDialog
      title: "New folder"
      property bool pending: false
      padding: Kirigami.Units.largeSpacing
      preferredWidth: Kirigami.Units.gridUnit * 20

      TextField {
        id: newFolderName
        placeholderText: "Folder name"
        width: Kirigami.Units.gridUnit * 16
      }

      Row {
        spacing: Kirigami.Units.smallSpacing
        Button {
          text: "Create"
          enabled: !newFolderDialog.pending
          onClicked: {
            if (hermes.createFolder(newFolderName.text))
              newFolderDialog.pending = true;
          }
        }
        Button {
          text: "Cancel"
          enabled: !newFolderDialog.pending
          onClicked: newFolderDialog.close()
        }
      }
    }

    Kirigami.Dialog {
      id: renameDialog
      title: "Rename"
      property bool pending: false
      padding: Kirigami.Units.largeSpacing
      preferredWidth: Kirigami.Units.gridUnit * 20

      TextField {
        id: renameName
        placeholderText: "New name"
        width: Kirigami.Units.gridUnit * 16
      }

      Row {
        spacing: Kirigami.Units.smallSpacing
        Button {
          text: "Rename"
          enabled: !renameDialog.pending
          onClicked: {
            const paths = directoryPage.selectedPaths;
            if (paths.length === 1) {
              const current = paths[0];
              const slash = current.lastIndexOf("/");
              const parentPath = slash > 0 ? current.substring(0, slash) : "";
              if (hermes.renameEntry(current, parentPath + "/" + renameName.text))
                renameDialog.pending = true;
            }
          }
        }
        Button {
          text: "Cancel"
          enabled: !renameDialog.pending
          onClicked: renameDialog.close()
        }
      }
    }

    Kirigami.Dialog {
      id: deleteConfirmDialog
      objectName: "deleteConfirmDialog"
      title: "Delete permanently?"
      property bool pending: false
      padding: Kirigami.Units.largeSpacing
      preferredWidth: Kirigami.Units.gridUnit * 24

      Text {
        width: Kirigami.Units.gridUnit * 20
        text: "This permanently deletes the selected entries and cannot be undone."
        wrapMode: Text.Wrap
        color: Kirigami.Theme.textColor
      }

      Row {
        spacing: Kirigami.Units.smallSpacing
        Button {
          objectName: "confirmDeleteButton"
          text: "Delete permanently"
          enabled: !deleteConfirmDialog.pending
          onClicked: {
            if (hermes.removeEntries(directoryPage.selectedPaths, false))
              deleteConfirmDialog.pending = true;
          }
        }
        Button {
          text: "Cancel"
          enabled: !deleteConfirmDialog.pending
          onClicked: deleteConfirmDialog.close()
        }
      }
    }
  }

  Connections {
    target: hermes
    function onOperationFailed(message) {
      operationErrorText.text = message;
      operationErrorDialog.open();
      newFolderDialog.pending = false;
      renameDialog.pending = false;
      deleteConfirmDialog.pending = false;
    }
    function onOperationFinished(kind, succeeded, errorCount) {
      if (kind === 0 && newFolderDialog.pending) {
        newFolderDialog.pending = false;
        if (succeeded) {
          newFolderName.text = "";
          newFolderDialog.close();
        }
      }
      if (kind === 1 && renameDialog.pending) {
        renameDialog.pending = false;
        if (succeeded) {
          renameDialog.close();
          directoryPage.clearSelection();
        }
      }
      if ((kind === 2 || kind === 3) && deleteConfirmDialog.pending) {
        deleteConfirmDialog.pending = false;
        if (succeeded)
          deleteConfirmDialog.close();
      }
      if (succeeded && (kind === 2 || kind === 3))
        directoryPage.clearSelection();
    }
  }

  Kirigami.Dialog {
    id: operationErrorDialog
    objectName: "operationErrorDialog"
    title: "Operation failed"
    standardButtons: Kirigami.Dialog.Ok
    padding: Kirigami.Units.largeSpacing
    preferredWidth: Kirigami.Units.gridUnit * 24
    Text {
      id: operationErrorText
      width: Kirigami.Units.gridUnit * 20
      wrapMode: Text.Wrap
      color: Kirigami.Theme.textColor
    }
  }
}
