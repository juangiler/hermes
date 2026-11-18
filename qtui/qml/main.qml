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
            action: Kirigami.Action {
              text: "Paste"
              icon.name: "edit-paste"
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
                directoryPage.clearSelection();
              }
            }
          }
          ToolButton {
            action: Kirigami.Action {
              text: "Delete"
              icon.name: "edit-delete"
              enabled: directoryPage.selectedPaths.length > 0
              onTriggered: {
                hermes.removeEntries(directoryPage.selectedPaths, false);
                directoryPage.clearSelection();
              }
            }
          }
        }
      }
    }

    Kirigami.Dialog {
      id: newFolderDialog
      title: "New folder"
      standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
      padding: Kirigami.Units.largeSpacing
      preferredWidth: Kirigami.Units.gridUnit * 20

      TextField {
        id: newFolderName
        placeholderText: "Folder name"
        width: parent.width
        onAccepted: newFolderDialog.accept()
      }

      onAccepted: {
        if (newFolderName.text.length > 0)
          hermes.createFolder(newFolderName.text);
        newFolderName.text = "";
      }
    }

    Kirigami.Dialog {
      id: renameDialog
      title: "Rename"
      standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
      padding: Kirigami.Units.largeSpacing
      preferredWidth: Kirigami.Units.gridUnit * 20

      TextField {
        id: renameName
        placeholderText: "New name"
        width: parent.width
        onAccepted: renameDialog.accept()
      }

      onAccepted: {
        const paths = directoryPage.selectedPaths;
        if (paths.length === 1 && renameName.text.length > 0) {
          const current = paths[0];
          const slash = current.lastIndexOf("/");
          const parentPath = slash > 0 ? current.substring(0, slash) : "";
          hermes.renameEntry(current, parentPath + "/" + renameName.text);
        }
        renameName.text = "";
        directoryPage.clearSelection();
      }
    }
  }
}
