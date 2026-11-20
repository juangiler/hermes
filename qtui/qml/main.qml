import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
  id: root
  title: "Hermes"

  // Qt 6.11 exposes no reduced-motion style hint, so this is the single switch
  // a desktop integration (or --reduced-motion) uses to stop pane motion.
  property int motionLevel: Qt.application.arguments.indexOf("--reduced-motion") >= 0 ? 1 : 0
  property int motionDuration: motionLevel > 0 ? 0 : 180
  property int renameTabIndex: 0
  property int focusedPaneId: {
    var tab = hermes.tabs[hermes.activeTab];
    return tab ? tab.focusedPane : 0;
  }

  function focusedPaneView() {
    for (var i = 0; i < paneRepeater.count; ++i) {
      var view = paneRepeater.itemAt(i);
      if (view && view.paneId === root.focusedPaneId)
        return view;
    }
    return null;
  }

  function focusedPage() {
    var view = root.focusedPaneView();
    return view ? view.directory : null;
  }

  function focusedSelectedPaths() {
    var page = root.focusedPage();
    return page ? page.selectedPaths : [];
  }

  function setFocusedViewMode(mode) {
    var page = root.focusedPage();
    if (page)
      page.setViewMode(mode);
    else
      hermes.setViewMode(mode);
  }

  // The controller has already removed the pane by the time this overlay is
  // created, so the fade never hides a directory the user is waiting for.
  function showCloseGhost(rect) {
    closeGhostComponent.createObject(paneArea, {
      "x": rect.x,
      "y": rect.y,
      "width": rect.width,
      "height": rect.height
    });
  }

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

  pageStack.initialPage: Kirigami.Page {
    id: mainPage
    title: "Directory"

    header: Column {
      width: parent.width

      Row {
        objectName: "tabBar"
        width: parent.width
        spacing: Kirigami.Units.smallSpacing
        Repeater {
          objectName: "tabRepeater"
          model: hermes.tabs
          delegate: ToolButton {
            id: tabButton
            objectName: "tabLabel_" + index
            property int tabIndex: index
            checkable: true
            checked: tabIndex === hermes.activeTab
            // The label is the C++ view of the tab: derived name, custom name
            // and split state all come from the controller.
            text: modelData.label
            onClicked: hermes.selectTab(tabIndex)

            contentItem: Row {
              spacing: Kirigami.Units.smallSpacing
              Text {
                anchors.verticalCenter: parent.verticalCenter
                text: tabButton.text
                color: Kirigami.Theme.textColor
              }
              Loader {
                active: modelData.multi
                sourceComponent: Text {
                  objectName: "multiBadge_" + tabButton.tabIndex
                  text: "MULTI"
                  color: Kirigami.Theme.highlightColor
                  anchors.verticalCenter: parent.verticalCenter
                }
              }
              ToolButton {
                objectName: "renameTabButton_" + tabButton.tabIndex
                icon.name: "edit-rename"
                onClicked: {
                  root.renameTabIndex = tabButton.tabIndex;
                  tabRenameField.text = modelData.customName;
                  tabRenameDialog.open();
                }
              }
              ToolButton {
                objectName: "closeTabButton_" + tabButton.tabIndex
                icon.name: "window-close"
                onClicked: hermes.closeTab(tabButton.tabIndex)
              }
            }
          }
        }
        ToolButton {
          objectName: "newTabButton"
          text: "New tab"
          onClicked: hermes.newTab()
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
              objectName: "splitRightButton"
              text: "Split right"
              onClicked: hermes.splitFocusedRight()
            }
            ToolButton {
              objectName: "splitDownButton"
              text: "Split down"
              onClicked: hermes.splitFocusedDown()
            }
            ToolButton {
              objectName: "closePaneButton"
              text: "Close pane"
              enabled: hermes.panes.length > 1
              onClicked: hermes.closeFocusedPane()
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
                enabled: root.focusedSelectedPaths().length === 1
                onTriggered: {
                  const current = root.focusedSelectedPaths()[0];
                  renameName.text = current.substring(current.lastIndexOf("/") + 1);
                  renameDialog.open();
                }
              }
            }
            ToolButton {
              action: Kirigami.Action {
                text: "Copy"
                icon.name: "edit-copy"
                enabled: root.focusedSelectedPaths().length > 0
                onTriggered: {
                  hermes.copySelection(root.focusedSelectedPaths(), false);
                  var page = root.focusedPage();
                  if (page)
                    page.clearSelection();
                }
              }
            }
            ToolButton {
              action: Kirigami.Action {
                text: "Cut"
                icon.name: "edit-cut"
                enabled: root.focusedSelectedPaths().length > 0
                onTriggered: {
                  hermes.copySelection(root.focusedSelectedPaths(), true);
                  var page = root.focusedPage();
                  if (page)
                    page.clearSelection();
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
                enabled: root.focusedSelectedPaths().length > 0
                onTriggered: {
                  hermes.removeEntries(root.focusedSelectedPaths(), true);
                }
              }
            }
            ToolButton {
              objectName: "deleteButton"
              action: Kirigami.Action {
                text: "Delete"
                icon.name: "edit-delete"
                enabled: root.focusedSelectedPaths().length > 0
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
              onClicked: root.setFocusedViewMode(0)
            }
            ToolButton {
              objectName: "detailsViewButton"
              text: "Details"
              checkable: true
              checked: hermes.viewMode === 1
              onClicked: root.setFocusedViewMode(1)
            }
            ToolButton {
              objectName: "gridViewButton"
              text: "Grid"
              checkable: true
              checked: hermes.viewMode === 2
              onClicked: root.setFocusedViewMode(2)
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

    Item {
      id: paneArea
      anchors.fill: parent
      clip: true

      onWidthChanged: hermes.setContentSize(width, height)
      onHeightChanged: hermes.setContentSize(width, height)
      Component.onCompleted: hermes.setContentSize(width, height)

      Repeater {
        id: paneRepeater
        model: hermes.panes
        delegate: PaneView {
          paneId: modelData.id
          directoryModel: modelData.model
          focused: modelData.id === root.focusedPaneId
          motionDuration: root.motionDuration
          x: modelData.x
          y: modelData.y
          width: modelData.width
          height: modelData.height
        }
      }

      Connections {
        target: hermes
        function onPaneClosed(rect) {
          Qt.callLater(root.showCloseGhost, rect)
        }
      }
    }

    Kirigami.Dialog {
      id: tabRenameDialog
      objectName: "tabRenameDialog"
      title: "Rename tab"
      padding: Kirigami.Units.largeSpacing
      preferredWidth: Kirigami.Units.gridUnit * 20

      TextField {
        id: tabRenameField
        objectName: "tabRenameField"
        placeholderText: "Tab name"
        width: Kirigami.Units.gridUnit * 16
      }

      Row {
        spacing: Kirigami.Units.smallSpacing
        Button {
          objectName: "confirmTabRenameButton"
          text: "Rename"
          onClicked: {
            hermes.renameTab(root.renameTabIndex, tabRenameField.text);
            tabRenameDialog.close();
          }
        }
        Button {
          text: "Cancel"
          onClicked: tabRenameDialog.close()
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
            const paths = root.focusedSelectedPaths();
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
            if (hermes.removeEntries(root.focusedSelectedPaths(), false))
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
      var page = root.focusedPage();
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
          if (page)
            page.clearSelection();
        }
      }
      if ((kind === 2 || kind === 3) && deleteConfirmDialog.pending) {
        deleteConfirmDialog.pending = false;
        if (succeeded)
          deleteConfirmDialog.close();
      }
      if (succeeded && (kind === 2 || kind === 3) && page)
        page.clearSelection();
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

  Component {
    id: closeGhostComponent
    Rectangle {
      id: ghost
      objectName: "paneCloseGhost"
      // Captured at creation: the controller's pane list must already be the
      // post-close one before the overlay exists.
      property int paneCount: hermes.panes.length
      property alias fade: fadeAnimation
      color: "transparent"
      border.color: Kirigami.Theme.highlightColor
      border.width: 2
      opacity: 0.7

      NumberAnimation on opacity {
        id: fadeAnimation
        objectName: "paneCloseFade"
        from: 0.7
        to: 0
        duration: root.motionDuration
        easing.type: Easing.OutQuad
        running: true
        onFinished: ghost.destroy()
      }
    }
  }
}
