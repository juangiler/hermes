# Kirigami Split Panes (slice S2a) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Kirigami window keeps its one tab strip; every tab holds its own tree of panes, and the focused pane in the active tab receives navigation, selection and commands.

**Architecture:** A per-tab `PaneTree` in C++ owns the split nodes and the pane rectangles; `ShellController` owns the ordered tabs, one `DirectoryModel` and one `DirectoryWatcher` per pane, and the per-pane state. QML places one reusable directory component at each pane rectangle inside the active tab, draws one tab strip with a `MULTI` badge and a user-settable tab name, and animates split and close transitions. No engine behaviour moves to C++; the C ABI stays unchanged in this slice.

**Tech Stack:** C++17, Qt 6.11 (Core, Gui, Qml, Quick, QuickControls2, Test), Kirigami, QML, CMake, Qt Test in `tools/qtui-selftest.cpp`.

**Spec:** `docs/superpowers/specs/2026-11-20-kirigami-split-panes-design.md` (read it before starting; it is the authority whenever this plan and the older commit messages disagree).

This is the first of the slice's two pull requests. S2b adds the Qt-owned session file and drag-and-drop; it is a separate plan because it is the first work to touch shared settings code.

## Global Constraints

- The user reversed the original panel-shape decision: **one tab strip for the window, a tree of panes per tab**. A tab that was never split shows one pane; switching tabs shows that tab's own arrangement. Tab names come from the focused pane's display name unless the user renamed the tab; a tab holding more than one pane shows a `MULTI` badge.
- The C engine sources stay pure ASCII and warning-clean: no non-ASCII byte in `src/*.c` or `src/*.h`, and no `warning:` line in the build log. CI fails the build on either.
- The legacy SDL application keeps building and keeps passing its `--selftest*` suites. This plan does not touch legacy sources; SDL is scaffolding and is deleted at the end of the port, so no format is preserved for its sake.
- All seven required CI check names stay unchanged: `source-checks`, `package-versions`, `selftest`, and the four `build (...)` matrix legs.
- Model mutation happens only on the GUI thread. The GUI thread never blocks on engine work.
- The shipped QML renders through Qt Quick's hardware scene graph. No `Canvas`, `QPainter` or `QGraphicsView` in shipped code, and no forced software backend. Offscreen software rendering is test-only.
- Motion never delays content: rows appear as soon as they are ready; animations are drawn on top of content that is already there. No expensive bindings or per-frame allocation.
- No engine behaviour is reimplemented in C++ or QML. Scanning, sorting, hidden-entry filtering and folder sizes stay in C.
- KDE Plasma 6.6 - 6.8 is the target. Verified means builds and headless tests; live Plasma behaviour is target-only.
- Local commits only. Do not push, tag or rewrite history. The SSH signing agent refuses signatures in this session; commits are created with `-c commit.gpgsign=false` and that is recorded in the ledger, not worked around by changing the repository's git config.
- Build and test inside the container: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && ...'`. The host has no SDL3 development packages.
- `docs/verified-commits.md` is pre-existing untracked user work: never read, stage or modify it.

## Review Focus

1. **A tab switch while a pane's directory is still being scanned.** The new tab's focused pane must never show the old tab's rows. Test `controllerDropsTheOldScanWhenTheFocusedPaneChanges` in Task 2.
2. **A pane closed while it holds a selection.** No selection may survive that a later operation could act on. Test `closingAPaneDropsItsSelection` in Task 4.
3. **The seventeenth pane.** The legacy refuses a seventeenth panel; the Qt side must refuse rather than build a broken tree. Test `controllerRefusesASeventeenthPane` in Task 2.
4. **A pane whose directory was deleted while the window was open.** The watcher and model must not crash and the pane keeps its rows, as the legacy does. Test `controllerKeepsTheRowsWhenAPaneDirectoryDisappears` in Task 2.
5. **Two panes of the same directory.** Each has its own model, history, selection and cursor; navigating one must not disturb the other. Test `controllerKeepsTwoPanesOfTheSameDirectoryIndependent` in Task 2.

---

### Task 1: The pane tree as plain data

**Files:**
- Create: `qtui/pane_tree.h`, `qtui/pane_tree.cpp`
- Delete: `qtui/panel_tree.h`, `qtui/panel_tree.cpp` (the rejected panel-shaped tree; `git rm` them)
- Modify: `CMakeLists.txt`, `tools/qtui-selftest.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces (this header is pinned; keep every line verbatim and add no other public API):

```cpp
// qtui/pane_tree.h
#pragma once
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

class DirectoryModel;
class DirectoryWatcher;

// One pane: one directory view and the state that belongs to it. The model and
// the watcher belong to the pane but are owned by the controller.
struct PaneState {
  QString path;
  QStringList history;
  int historyIndex = -1;
  QStringList selectedPaths;  // the selection, as the paths the view shows
  int currentIndex = 0;       // the cursor row
  int anchorIndex = 0;        // the selection anchor
};

struct Pane {
  PaneState state;
  DirectoryModel *model = nullptr;
  DirectoryWatcher *watcher = nullptr;
};

// A node of a tab's split tree. A leaf holds a pane; a split holds two children
// and the ratio between them.
struct PaneNode {
  enum Kind { PaneLeaf, HSplit, VSplit };
  Kind kind = PaneLeaf;
  double ratio = 0.5;
  int paneIndex = -1;               // a leaf's pane
  std::unique_ptr<PaneNode> a, b;   // a split's children
};

// The pane tree of one tab and the operations on it.
class PaneTree {
 public:
  static constexpr int MaxPanes = 16;

  // Which side of the split the new pane takes. Left and Top put it first.
  enum class Zone { Left, Right, Top, Bottom };

  PaneTree();  // one pane with one empty path
  QVector<Pane> &panes();
  const QVector<Pane> &panes() const;
  PaneNode *root() const;
  int focusedPane() const;
  void setFocusedPane(int index);

  // Splits the pane and returns the new pane's index, or -1 at MaxPanes.
  // The new pane shows path and becomes the focused pane.
  int splitPane(int paneIndex, Zone zone, const QString &path);

  // Removes the pane and its state. The sibling of its leaf takes the leaf's
  // place, so the space it leaves goes to that sibling. If the removed pane
  // was focused, the first pane of the promoted sibling subtree in reading
  // order becomes focused.
  void closePane(int paneIndex);

  // The panes in reading order: left to right, top to bottom.
  QVector<int> panesInReadingOrder() const;

  // The rectangle of every pane, in the order of panes().
  QVector<QRect> layout(const QSize &area) const;

 private:
  QVector<Pane> m_panes;
  std::unique_ptr<PaneNode> m_root;
  int m_focused_pane = 0;
};
```

The layout follows the legacy exactly: a split gives the first child `round(size * ratio)` and the second the rest minus a one pixel gap (`src/app.c:1570`). Splitting, closing and focus all-or-nothing: a refused split leaves the tree and its state untouched.

- [ ] **Step 1: Write the failing tests**

In `tools/qtui-selftest.cpp`, remove the ten `panelTree*` methods and their registrations, include `pane_tree.h` instead of `panel_tree.h`, and add these methods with these assertions:

- `paneTreeSplitsRightAndDown`: a fresh tree has one pane; `splitPane(0, PaneTree::Zone::Right, "/tmp")` returns 1; the root becomes `PaneNode::HSplit` with ratio 0.5; `panes().at(1).state.path` is `/tmp`; a `Bottom` split of another fresh tree makes a `VSplit`.
- `paneTreePlacesTheNewPaneOnTheChosenSide`: `Left` and `Top` give `panesInReadingOrder() == QVector<int>({1, 0})`; `Right` and `Bottom` give `{0, 1}`.
- `paneTreeRefusesTheSixteenthPane`: split until `MaxPanes`; the next split returns -1 and both `panes().size()` and `panesInReadingOrder()` are unchanged.
- `paneTreeClosesAPaneAndTheSiblingTakesTheSpace`: in a two-pane tree, closing pane 0 leaves one pane, a leaf root pointing at pane 0, and the surviving pane's path; closing pane 1 of a three-pane chain leaves the horizontal split with panes 0 and 1 in reading order and pane 1 holding the third pane's path.
- `paneTreeLayoutSplitsTheArea`: `layout(QSize(800, 600))` on a horizontal split gives widths 400 and 399 and heights 600; a vertical split gives heights 300 and 299 and widths 800.
- `paneTreeFocusesTheNewPaneOnSplit`: `focusedPane()` is 1 after a right split of pane 0.
- `paneTreeFocusesTheFirstPaneWhenTheFocusedPaneCloses`: split 0 right, split 1 bottom, set focus to pane 1, close pane 1; the focused pane is the first pane in reading order of the promoted subtree.
- `paneTreeKeepsFocusWhenAnotherPaneCloses`: in a three-pane tree with focus on pane 2, closing pane 0 leaves focus on the pane that inherited pane 2's index.
- `paneTreeKeepsPaneStateTogether`: set pane 0's state path, history, selection, cursor and anchor; split pane 0; the new pane's state is independent and the original state is intact.
- `paneTreeRefusedSplitLeavesTheTreeUntouched`: after the limit is reached, a refused split changes no pane, no node and no focus.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-qt-selftest 2>&1 | tail -5'`

Expected: the build fails because `pane_tree.h` does not exist yet.

- [ ] **Step 3: Implement `qtui/pane_tree.{h,cpp}`**

Port the reviewed tree surgery from `panel_tree.cpp` verbatim where the shape is unchanged, changing only the leaf payload from a tab stack to a `Pane`. Keep these legacy rules from Task 1's review: the current focus adjusts when an earlier pane is closed; closing the focused pane activates the first leaf of the sibling subtree in reading order; a split focuses the new pane; every mutation is all-or-nothing; the sibling promotion maps a removed pane's pre-removal index to its post-removal index. Use two-space indent and `snake_case` members, as the existing `qtui/` files do.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-qt hermes-qt-selftest 2>&1 | tee /tmp/s2-t1.log && QT_QPA_PLATFORM=offscreen ./build/cmake-final/hermes-qt-selftest 2>&1 | tail -3 && ! grep -q "warning:" /tmp/s2-t1.log'`

Expected: all tests pass and the build log has no `warning:` line. The controller and QML still compile against the tree shims from the previous commits; if they do not, keep temporary includes/typedefs local to `shell_controller.cpp` and remove them in Task 2.

- [ ] **Step 5: Commit**

```bash
git add qtui/pane_tree.h qtui/pane_tree.cpp qtui/panel_tree.h qtui/panel_tree.cpp CMakeLists.txt tools/qtui-selftest.cpp
git -c commit.gpgsign=false commit -m "refactor(ui): make the tree hold panes instead of panels"
```

---

### Task 2: Tabs own the trees, panes own the state

**Files:**
- Modify: `qtui/shell_controller.h`, `qtui/shell_controller.cpp`, `tools/qtui-selftest.cpp`

**Interfaces:**
- Consumes: `PaneTree`, `Pane`, `PaneNode`, `PaneState` from Task 1.
- Produces:

```cpp
// qtui/shell_controller.h
Q_PROPERTY(QVariantList tabs READ tabs NOTIFY tabsChanged)       // one map per tab
Q_PROPERTY(int activeTab READ activeTab NOTIFY activeTabChanged)
Q_PROPERTY(QVariantList panes READ panes NOTIFY tabsChanged)     // active tab's panes
Q_PROPERTY(DirectoryModel *directory READ directory NOTIFY focusedPaneChanged)
// the S1 properties (viewMode, columns, zoom, showHidden, folderSizes, sortKey,
// canGoBack, canGoForward, canPaste) stay and act on the focused pane.

int activeTab() const;
QVariantList tabs() const;
QVariantList panes() const;

Q_INVOKABLE void selectTab(int index);
Q_INVOKABLE void closeTab(int index);
Q_INVOKABLE void newTab();
Q_INVOKABLE void renameTab(int index, const QString &name);
Q_INVOKABLE void setFocusedPane(int paneId);
Q_INVOKABLE void closeFocusedPane();
Q_INVOKABLE void splitFocusedRight();
Q_INVOKABLE void splitFocusedDown();
Q_INVOKABLE void openInSplit(const QString &path, bool right);
Q_INVOKABLE void cyclePane(int direction);
Q_INVOKABLE void setContentSize(int width, int height);

Q_INVOKABLE QStringList selectedPaths(int paneId) const;
Q_INVOKABLE void setSelectedPaths(int paneId, const QStringList &paths);
Q_INVOKABLE int currentIndex(int paneId) const;
Q_INVOKABLE void setCurrentIndex(int paneId, int index);
Q_INVOKABLE int anchorIndex(int paneId) const;
Q_INVOKABLE void setAnchorIndex(int paneId, int index);

signals:
  void tabsChanged();
  void activeTabChanged();
  void focusedPaneChanged();
  void paneClosed(const QRect &rect);  // the closed pane's rect, emitted before removal
```

Each entry of `tabs()` is a `QVariantMap` with `label`, `customName`, `multi` (bool), `focusedPane`, `path` (the focused pane's displayed path) and `index`. Each entry of `panes()` is a `QVariantMap` with `id`, `path`, `model` (`DirectoryModel *`), `x`, `y`, `width`, `height`. Geometry comes from `PaneTree::layout()` against the size set by `setContentSize()`.

The tab label is the focused pane's display name by the legacy's `disp_name()` rule (`src/app.c:1144`): the base name, `/` for root, `Trash` for the trash directory, with a trailing `::` stripped from a remote path. `renameTab(index, name)` sets the custom name; an empty name clears it and restores the derived one.

The panel-shaped API from the earlier commits (`activePanel`, `panels()`, `selectTab(int,int)`, `closeTab(int,int)`, `newTabInPanel`, `moveTabToSplit`, `cyclePanel`, `activatePanel`) stays as thin compatibility shims in this task, because the shipped QML and several S1 QML tests still bind to it. Task 3 removes the shims when it rewrites the QML. The host hook and the engine-facing `hostCurrentTab()` must keep working off the GUI thread via an atomic mirror of the active tab index; the engine thread never touches the tab vector or the tree.

- [ ] **Step 1: Write the failing tests**

In `tools/qtui-selftest.cpp`, remove the twelve `controller*` panel-shaped methods and their helpers, and add these. Use a QTemporaryDir for every path. Register each in `private slots:`.

- `controllerSplitsTheFocusedPane`: a controller with one tab and one pane; `splitFocusedRight()` gives `panes().size() == 2`, the new pane shows the old pane's path, and `tabs().at(0).toMap().value("multi")` is true.
- `controllerOpensAFolderInASplit`: `openInSplit(dir, true)` gives two panes and the new one shows `dir`.
- `controllerRefusesASeventeenthPane`: split until `PaneTree::MaxPanes`; the next split leaves `panes().size()` and the pane paths unchanged.
- `controllerNavigatesInTheFocusedPaneOnly`: two panes on different directories; navigating the focused pane does not change the other pane's path.
- `controllerKeepsTwoPanesOfTheSameDirectoryIndependent`: two panes on one directory; navigating one leaves the other's path unchanged and each has its own model pointer and history.
- `controllerDropsTheOldScanWhenTheFocusedPaneChanges`: create a tab with a slow-to-scan directory, start a scan, switch to another tab and back; the focused pane's model path is the new tab's path and `model->paths()` never contains the old tab's entries. Use `QTRY_VERIFY` with a bounded timeout.
- `controllerKeepsTheRowsWhenAPaneDirectoryDisappears`: scan a directory, remove it with `QDir::rmdir`, claim an operation completion; the pane keeps its rows and the controller does not crash.
- `controllerCyclesThePanes`: with three panes in reading order, `cyclePane(1)` moves focus through them and wraps; `cyclePane(-1)` goes back.
- `controllerRemembersEachTabsPaneTree`: split tab 0, create tab 1 (one pane), split tab 1 down, switch to tab 0 and assert two panes with the first arrangement, then switch to tab 1 and assert two panes with the vertical arrangement; the two tabs' models are distinct.
- `controllerKeepsTheFocusPerTab`: focus pane 1 in tab 0, switch to tab 1, switch back, and assert tab 0 still focuses pane 1 while tab 1 kept its own focus.
- `controllerLabelsATabFromItsFocusedPane`: the tab's `label` is the focused pane's base name; navigate the focused pane and the label follows; for `/` it is `/`, and a path ending in `::` loses the `::`.
- `controllerRenamesATabAndClearingRestoresTheDerivedName`: `renameTab(0, "Work")` makes `label` "Work" and `customName` "Work"; `renameTab(0, "")` restores the derived label.
- `controllerMarksASplitTabAsMulti`: `multi` is false for one pane, true after a split, and false again after `closeFocusedPane()`.
- `controllerClosesAFocusedPaneAndReportsItsRect`: `paneClosed` fires with the removed pane's rectangle before `panes()` shrinks, and the sibling inherits the space.

Adapt the seven surviving S1 tests to the new API without weakening what they prove: `navigationNotifiesTabLabels`, `closeTabBeforeCurrentKeepsFocusedTab`, `focusFreeHostTabKeepsValidCursor`, `hostOpenTabAtIndexInsertsTab`, `selectionUsesPathListAndClearsOnNavigation`, `failedOperationRefreshPreservesSelection` and `hostHooksAreSafeAcrossControllerLifetime`. Where a test asserted on the panel list, assert on `tabs()` and `panes()` instead; where it used `selectTab(0, index)`, use `selectTab(index)` after selecting tab 0. The engine hook tests keep using `openTabFromHost()`.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-qt-selftest 2>&1 | tail -5'`

Expected: the build fails because `activeTab`, `splitFocusedRight` and `panes` do not exist yet.

- [ ] **Step 3: Implement the controller changes**

- Replace `PanelTree m_tree` with `QVector<TabState> m_tabs`, where `TabState` holds a `PaneTree` and a `QString custom_name`, plus `int m_active_tab`. Keep the existing operations and settings code; only the target pane changes.
- Create one `DirectoryModel` and one `DirectoryWatcher` per pane when a pane is created, parented to the controller, and destroy them when the pane closes. Every pane's watcher follows that pane's directory, not only the focused one's. When a pane's path changes, its model and watcher follow.
- All navigation, history, clipboard, file operations and the raw-path operation boundary act on the active tab's focused pane. Preserve the S1 unique/NotListed behaviour of `resolveRawPaths()`; only the model it resolves against changes.
- `splitFocusedRight/Down` split the active tab's focused pane with `Zone::Right`/`Zone::Bottom`, opening the focused pane's path in the new pane and focusing it. `openInSplit` does the same with the given path. `closeFocusedPane()` closes the leaf, emits `paneClosed(rect)` with the rect captured before removal, and drops the pane's selection and watcher.
- Keep one atomic mirror of the focused pane's state for `hostCurrentTab()`; the engine thread must never touch the tab vector or the tree.
- Emit `tabsChanged()` when any tab's tree, label or focus changes; emit `focusedPaneChanged()` when focus moves; emit `activeTabChanged()` on tab switches. Geometry recomputes on `setContentSize()` and emits `tabsChanged()`.
- Do not add shortcuts or session persistence in this task.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-qt hermes-qt-selftest 2>&1 | tee /tmp/s2-t2.log && QT_QPA_PLATFORM=offscreen ./build/cmake-final/hermes-qt-selftest 2>&1 | tail -3 && ! grep -q "warning:" /tmp/s2-t2.log'`

Expected: all tests pass, including the unchanged QML tests that still use the compatibility shims, and no `warning:` line.

- [ ] **Step 5: Commit**

```bash
git add qtui/shell_controller.h qtui/shell_controller.cpp tools/qtui-selftest.cpp
git -c commit.gpgsign=false commit -m "feat(ui): give each tab its own pane tree"
```

---

### Task 3: The window draws one tab strip and the active tab's panes

**Files:**
- Modify: `qtui/qml/main.qml`, `qtui/qml/DirectoryPage.qml`
- Modify: `CMakeLists.txt` (resource list if a new QML file is added)
- Test: `tools/qtui-selftest.cpp`

**Interfaces:**
- Consumes: `hermes.tabs`, `hermes.activeTab`, `hermes.panes`, the commands from Task 2 and `hermes.paneClosed(rect)`.
- Produces: QML objects with the pinned `objectName`s listed below.

- [ ] **Step 1: Write the failing tests**

In `tools/qtui-selftest.cpp`, replace the four panel-shaped QML tests with these. Load the real window first, then act on it, as PR 3 established.

- `windowShowsOnePanePerTreeLeaf`: split the focused pane right and assert that `paneView_0` and `paneView_1` exist and each contains a `directoryPane_<id>`; close the focused pane and assert only one pane view remains.
- `switchingTabsShowsThatTabsPaneTree`: tab 0 has two panes, tab 1 has one; clicking `tabLabel_1` leaves exactly one `paneView_*` in the window, and clicking `tabLabel_0` restores two.
- `multiBadgeAppearsOnlyForSplitTabs`: `multiBadge_0` does not exist initially, exists after a split, and is gone after closing the second pane.
- `renameTabUpdatesTheLabel`: click `renameTabButton_0`, type "Work" into `tabRenameField`, click `confirmTabRenameButton`; `tabLabel_0`'s text is "Work" and `hermes.tabs[0].customName` is "Work". Clearing the field restores the derived name.
- `splitAndCloseAreAnimated`: after clicking `splitRightButton`, an object named `paneOpenAnimation` is running; after clicking `closePaneButton`, an object named `paneCloseGhost` exists with a running `NumberAnimation` on `opacity`. The controller's pane list has already changed when the ghost appears - the animation does not delay content.
- `theToolbarActsOnTheFocusedPane`: with two panes on different directories, click `detailsViewButton` and assert the focused pane's view mode changed while the other pane kept its own; click a pane view to focus it and repeat.

Also adapt the S1 QML tests that still look for the old tab strip (`tabManagementHasReadableLabels`, `navigationNotifiesTabLabels` if it loads QML) to the new object names, keeping their assertions. Remove the Task 2 `QSKIP`s.

Pinned object names: `tabBar`, `tabRepeater`, `tabLabel_<index>`, `closeTabButton_<index>`, `newTabButton`, `multiBadge_<index>`, `renameTabButton_<index>`, `tabRenameDialog`, `tabRenameField`, `confirmTabRenameButton`, `splitRightButton`, `splitDownButton`, `closePaneButton`, `paneView_<id>`, `directoryPane_<id>`, `paneOpenAnimation`, `paneCloseGhost`.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-qt-selftest 2>&1 | tail -5'`

Expected: the new tests fail because the tab strip has no rename or `MULTI` badge, no split buttons and no pane views.

- [ ] **Step 3: Implement the QML**

- `main.qml`: keep the single `Kirigami.ApplicationWindow` and the toolbar. Replace the plain `DirectoryPage` page with a `Kirigami.Page` whose header holds the tab strip and toolbar and whose content holds an `Item` with a `Repeater` over `hermes.panes`. Each delegate is a `PaneView` placed at `x`, `y`, `width`, `height` from the model and named `paneView_<id>`. Call `hermes.setContentSize(width, height)` when the content area changes. Remove the panel-shaped compatibility shims from `shell_controller.{h,cpp}` once no QML or test binds to them.
- The tab strip's buttons call `selectTab(index)`, `closeTab(index)`, `newTab()` and `renameTab(index, name)`. Each button's text uses `modelData.label` from C++, never a path-splitting expression in QML. A `Text` reading `MULTI` named `multiBadge_<index>` is visible only when `modelData.multi` is true.
- `DirectoryPage.qml` becomes the reusable directory content: the three views, the size/date helpers and the keyboard handling, taking a `paneId` and a `model` property. Its root object name is `directoryPane_<paneId>`; it renders from `hermes.selectedPaths(paneId)` and `hermes.currentIndex(paneId)` with the accessors from Task 2. Wrap it in a small `PaneView.qml` if that keeps `main.qml` readable; add its file to the resource list in `CMakeLists.txt` if so.
- Motion: define one `property int motionDuration: 180` on the window and use it for a `Behavior` on each pane delegate's `x`, `y`, `width` and `height` (`NumberAnimation`, ease-out), and for the new pane's `opacity` fade (`paneOpenAnimation`). On `hermes.paneClosed(rect)`, create a temporary transparent overlay rectangle named `paneCloseGhost` at that rect with a `NumberAnimation` fading `opacity` to 0 and `Component.onCompleted: destroy()` after it finishes; the real pane is already gone. This satisfies the design's rule that motion draws on content that is already there. Honour the platform's reduced-motion setting through a `motionLevel` property that sets the duration to 0 when reduced motion is requested.
- Keep the existing dialogs and error handling working against the new selection accessors.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-qt hermes-qt-selftest 2>&1 | tee /tmp/s2-t3.log && QT_QPA_PLATFORM=offscreen ./build/cmake-final/hermes-qt-selftest 2>&1 | tail -3 && ! grep -q "warning:" /tmp/s2-t3.log'`

Expected: all tests pass and no `warning:` line.

- [ ] **Step 5: Commit**

```bash
git add qtui/qml/main.qml qtui/qml/DirectoryPage.qml qtui/qml/PaneView.qml CMakeLists.txt tools/qtui-selftest.cpp
git -c commit.gpgsign=false commit -m "feat(ui): draw the active tab's pane tree"
```

---

### Task 4: Selection and cursor belong to the pane

**Files:**
- Modify: `qtui/directory_model.h`, `qtui/directory_model.cpp`, `qtui/qml/DirectoryPage.qml`, `tools/qtui-selftest.cpp`

**Interfaces:**
- Consumes: `PaneState` from Task 1, the pane-parameterised accessors from Task 2 and the reusable component from Task 3.
- Produces: no new API; this task completes the move of state out of QML.

- [ ] **Step 1: Write the failing tests**

In `tools/qtui-selftest.cpp`, add these methods and register them. Load the real window for each: a controller-only assertion would pass already, because `PaneState` has had these fields since Task 2. The tests must fail because `DirectoryPage.qml` still owns the selection.

- `selectionSurvivesATabSwitch`: click a row in a pane, switch to the other tab and back, and assert `selectedPaths(paneId)` is unchanged.
- `selectionDoesNotLeakBetweenPanes`: select a row in one pane and assert `selectedPaths(otherPaneId)` is empty.
- `closingAPaneDropsItsSelection`: select a row in a pane, close that pane, and assert no remaining pane has that selection and no later operation can use it.
- `theCursorSurvivesATabSwitch`: set the cursor on a row, switch tabs and back, and assert `currentIndex(paneId)` is unchanged.
- `selectionDropsEntriesRemovedByARescan`: select a row through the view, replace the model's rows so that path is gone, and assert the selection no longer contains it.
- `ambiguousDisplayedPathRefusesTheOperation`: two entries whose displayed paths are identical but whose raw bytes differ; put that displayed path in the selection and assert `removeEntries()` refuses visibly and does not remove either entry.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-qt-selftest 2>&1 | tail -5'`

Expected: the selection tests fail because the QML component still owns `selectedPaths` and `currentIndex`.

- [ ] **Step 3: Move the state**

- Remove `property var selectedPaths` and `property int currentIndex` from the directory component; read and write the pane's state through the controller accessors from Task 2, including the anchor for shift-selection.
- Keep the selection as displayed paths. The raw bytes stay the model's business; `resolveRawPaths()` keeps the S1 uniqueness refusal and the `NotListed` behaviour.
- When a scan replaces the rows, drop displayed paths that are no longer present. When a pane closes, clear its state before its model and watcher are destroyed.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-qt hermes-qt-selftest 2>&1 | tee /tmp/s2-t4.log && QT_QPA_PLATFORM=offscreen ./build/cmake-final/hermes-qt-selftest 2>&1 | tail -3 && ! grep -q "warning:" /tmp/s2-t4.log'`

Expected: all tests pass and no `warning:` line.

- [ ] **Step 5: Commit**

```bash
git add qtui/directory_model.h qtui/directory_model.cpp qtui/qml/DirectoryPage.qml tools/qtui-selftest.cpp
git -c commit.gpgsign=false commit -m "feat(ui): keep the selection and cursor in pane state"
```

---

### Task 5: Documentation and changelog

**Files:**
- Modify: `docs/architecture.md`, `docs/building.md`, `CHANGELOG.md`

**Interfaces:**
- Consumes: what Tasks 1 to 4 produced.
- Produces: nothing.

- [ ] **Step 1: Describe the pane tree in `docs/architecture.md`**

Add `qtui/pane_tree.*` to the Qt/Kirigami shell section: a per-tab tree of panes, the split and close operations, and the pane rectangles. Update the paragraphs for `qtui/shell_controller.*` (it owns the ordered tabs, each tab's tree, and one model and watcher per pane; the commands act on the active tab's focused pane) and `qtui/directory_model.*` (one instance per pane).

- [ ] **Step 2: Describe the behaviour in `docs/building.md`**

Say that the window has one tab strip, that each tab can hold a tree of split panes, that the focused pane receives commands, that a split tab is marked `MULTI` and can be renamed, and that the arrangement is not yet persisted - the Qt-owned session arrives in S2b. Keep the note that the offscreen suites verify headless plumbing only and not live KDE Plasma behaviour.

- [ ] **Step 3: Add the changelog entry**

In `CHANGELOG.md`'s "Unreleased" section, add a bullet under "**New**" describing the user-visible result: a tab can be split into panes, tabs remember their own layout and can be renamed.

- [ ] **Step 4: Verify the docs**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && python3 tools/build-install-selftest.py && python3 tools/branding-selftest.py && python3 tools/legacy-purge-selftest.py && sh tools/check-release-version.sh'`

Expected: all three Python suites pass and the release-version check reports the current version.

- [ ] **Step 5: Commit**

```bash
git add docs/architecture.md docs/building.md CHANGELOG.md
git -c commit.gpgsign=false commit -m "docs(ui): describe the pane tree per tab"
```

---

## Verification

Run all of it before the pull request, in the container:

```bash
distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake -S . -B build/cmake-final -DHERMES_BUILD_QT_UI=ON -DHERMES_DEFAULT_UI=legacy && cmake --build build/cmake-final --target hermes-legacy hermes-qt hermes-qt-selftest hermes-core-selftest hermes-core-abi-selftest hermes-cpp-probe hermes-portal-selftest 2>&1 | tee /tmp/s2a.log'
```

- no `warning:` line in the log
- `QT_QPA_PLATFORM=offscreen ./build/cmake-final/hermes-qt-selftest` - all pass
- `./build/cmake-final/hermes-core-selftest`, `hermes-core-abi-selftest`, `hermes-cpp-probe`, and `sh tools/portal-selftest.sh`
- `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && ./build.sh debug && ./build/debug/hermes --selftest-ops'`
- `python3 tools/build-install-selftest.py && python3 tools/branding-selftest.py && python3 tools/legacy-purge-selftest.py`
- `LC_ALL=C grep -nP '[^\x00-\x7F]' src/*.c src/*.h` - no output
- `git diff --check`
- `grep -nE 'Canvas|QPainter|QGraphicsView|QT_QUICK_BACKEND' qtui/qml/*` - no shipped software fallback

## What is left to the builder

- The QML component split: whether `PaneView.qml` is worth a separate file or belongs inside `main.qml`.
- The exact wording and placement of the rename affordance and the `MULTI` badge.
- Whether `paneClosed(rect)` carries a `QRect` or four integers to QML; keep the test's object names either way.
