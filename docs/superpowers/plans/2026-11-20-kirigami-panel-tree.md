# Kirigami Panel Tree (slice S2a) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Kirigami window holds a tree of panels; every panel has its own tab strip and its own stack of tabs, and a panel can be split right or down.

**Architecture:** The panel tree is plain C++ in a new `qtui/panel_tree.{h,cpp}`, owned by `ShellController`, which also owns one `DirectoryModel` and one `DirectoryWatcher` per panel. The controller lays the panels out - walking the tree and giving every panel a rectangle, the way the legacy computes its panel rectangles in `layout()` (`src/app.c:1570`) - and the QML places one view per panel at that rectangle. Navigation, history, the clipboard and the commands act on the active panel's current tab.

**Tech Stack:** C++17, Qt 6.11 (Core, Gui, Qml, Quick, QuickControls2, Test), Kirigami, QML, CMake, plain assertions in the C suites and Qt Test in the Qt suite.

**Spec:** `docs/superpowers/specs/2026-11-20-kirigami-split-panes-design.md` (the design doc travels with this plan; read it before starting).

This is the first of the slice's two pull requests. S2b - the session persistence and the dragging - is a separate plan and a separate pull request, because it is the first work to touch the legacy SDL application's settings code.

## Global Constraints

- The C engine sources stay pure ASCII and warning-clean: no non-ASCII byte in `src/*.c` or `src/*.h`, and no `warning:` line in the build log. CI fails the build on either.
- The legacy SDL application keeps building and keeps passing its `--selftest*` suites. This plan does not touch the legacy application at all.
- All seven required CI check names stay unchanged: `source-checks`, `package-versions`, `selftest`, and the four `build (...)` matrix legs.
- Model mutation happens only on the GUI thread. The GUI thread never blocks on engine work.
- The shipped QML renders through Qt Quick's hardware scene graph. No `Canvas`, `QPainter` or `QGraphicsView` in shipped code, and no forced software backend. Offscreen software rendering is test-only.
- No engine behaviour is reimplemented in C++ or QML. Scanning, sorting, hidden-entry filtering and folder sizes stay in C.
- KDE Plasma 6.6 - 6.8 is the target. Verified means builds and headless tests; live Plasma behaviour is target-only.
- Local commits only. Do not push, tag or rewrite history.
- Build and test inside the container: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && ...'`. The host has no SDL3 development packages, so a host `./build.sh` fails for that reason alone.
- `docs/verified-commits.md` is pre-existing untracked user work: never read, stage or modify it.

## Review Focus

The failure modes a person using a split window would hit, that no task's tests would otherwise catch. Each line gets its test in the task that owns the code.

1. **A tab switch while the panel's directory is still being scanned.** The panel shows the new tab's directory, never the old tab's rows. The generation guard lives in the model, so re-pointing a model at another path must not let the old scan's result land.
2. **A panel closed while it holds a selection.** The selection belongs to the tab, so closing the panel must not leave a selection behind that a later operation could act on.
3. **The sixteenth panel.** The legacy refuses a seventeenth with a "Too many panels" message; the Qt side must refuse rather than build a broken tree.
4. **A panel whose directory was deleted while the window was open.** The watcher and the model must not crash and the panel must keep its rows, as the legacy's does.
5. **Two panels of the same directory.** Each has its own model, its own history and its own selection; navigating one must not disturb the other.

---

### Task 1: The panel tree as plain data

**Files:**
- Create: `qtui/panel_tree.h`, `qtui/panel_tree.cpp`
- Modify: `CMakeLists.txt` (add `qtui/panel_tree.cpp` to the `hermes-kirigami-ui` target)
- Test: `tools/qtui-selftest.cpp`

**Interfaces:**
- Consumes: nothing; this is the first task.
- Produces:

```cpp
// qtui/panel_tree.h
#pragma once
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

class DirectoryModel;

// One tab on a panel's strip: the directory it shows and the state that
// belongs to that tab rather than to the panel.
struct PanelTab {
  QString path;
  QStringList history;
  int historyIndex = -1;
  QStringList selectedPaths;  // the selection, as the paths the view shows
  int currentIndex = 0;       // the cursor row
  int anchorIndex = 0;        // the selection anchor
};

// A panel: a strip of tabs and the tab that is showing.
struct Panel {
  QVector<PanelTab> tabs;
  int currentTab = 0;
  DirectoryModel *model = nullptr;   // owned by the controller
};

// A node of the split tree. A leaf holds a panel; a split holds two children
// and the ratio between them.
struct PanelNode {
  enum Kind { PanelLeaf, HSplit, VSplit };
  Kind kind = PanelLeaf;
  double ratio = 0.5;
  int panelIndex = -1;               // a leaf's panel
  std::unique_ptr<PanelNode> a, b;   // a split's children
};

// The tree of panels and the operations on it.
class PanelTree {
public:
  static constexpr int MaxPanels = 16;

  // Which side of the split the new panel takes. Left and Top put it first.
  enum class Zone { Left, Right, Top, Bottom };

  PanelTree();  // one panel with one empty tab
  QVector<Panel> &panels();
  const QVector<Panel> &panels() const;
  PanelNode *root() const;
  int activePanel() const;
  void setActivePanel(int index);

  // Splits the panel and returns the new panel's index, or -1 when the limit
  // of MaxPanels is reached. When tabIndex is not negative that tab moves
  // into the new panel; otherwise the new panel opens path. The new panel is
  // placed on zone's side of the split.
  int splitPanel(int panelIndex, Zone zone, int tabIndex, const QString &path);

  // Removes the panel and its tabs. The sibling of its leaf takes the leaf's
  // place, so the space it leaves goes to that sibling.
  void closePanel(int panelIndex);

  // The panels in reading order: left to right, top to bottom.
  QVector<int> panelsInReadingOrder() const;

  // The rectangle of every panel, in the order of panels().
  QVector<QRect> layout(const QSize &area) const;
};
```

The layout follows the legacy exactly: a split gives the first child `round(size * ratio)` and the second the rest, minus a one pixel gap between them (`src/app.c:1570`).

- [ ] **Step 1: Write the failing tests**

In `tools/qtui-selftest.cpp` add these test methods and register them in the `private slots:` list:

- `panelTreeSplitsRightAndDown`: a fresh tree has one panel; `splitPanel(0, PanelTree::Zone::Right, -1, "/tmp")` returns 1 and the root becomes an `HSplit` with ratio 0.5; a `PanelTree::Zone::Down` split of a panel makes a `VSplit`.
- `panelTreePlacesTheNewPanelOnTheChosenSide`: `PanelTree::Zone::Left` makes the new panel the first child, so `panelsInReadingOrder()` is `{1, 0}`; `PanelTree::Zone::Right` makes it `{0, 1}`.
- `panelTreeMovesATabIntoTheNewPanel`: a panel with two tabs; splitting with `tabIndex = 0` moves that tab into the new panel and leaves the source panel with the other one.
- `panelTreeRefusesTheSixteenthPanel`: splitting until there are `MaxPanels` panels; the next `splitPanel` returns -1 and the tree is unchanged.
- `panelTreeClosesAPanelAndTheSiblingTakesTheSpace`: a tree of two panels; `closePanel(0)` leaves one panel and a root that is a leaf.
- `panelTreeLayoutSplitsTheArea`: `layout(QSize(800, 600))` on a horizontal split gives the first panel a width of 400 and the second 399 (the one pixel gap), both 600 high; a vertical split gives heights of 300 and 299.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-qt-selftest 2>&1 | tail -5'`

Expected: the build fails, because `qtui/panel_tree.h` does not exist yet.

- [ ] **Step 3: Implement `qtui/panel_tree.{h,cpp}`**

The header is exactly the block above. The implementation:

- `PanelTree()` creates one panel with one `PanelTab` whose path is empty, and a root that is a leaf pointing at panel 0.
- `splitPanel` replaces the leaf of `panelIndex` with a split node of ratio 0.5, puts the new panel on `zone`'s side (Left and Top put the new panel first, Right and Bottom put it second), creates the new panel with one tab - the moved tab or a tab with `path` - and returns the new panel's index. It returns -1 without touching the tree when the panel count would exceed `MaxPanels`.
- `closePanel` removes the leaf, letting its sibling take the leaf's place in the parent, and removes the panel from `panels()`. The panels after it shift down, so a panel's index is its position in `panels()`.
- `panelsInReadingOrder` walks the tree left to right, top to bottom, collecting the leaf panels.
- `layout` walks the tree with a rectangle: a leaf takes the whole rectangle; a horizontal split gives the first child `round(width * ratio)` and the second the rest minus one pixel, a vertical split does the same with the height. The result is in the order of `panels()`.

Follow the existing style in `qtui/`: two space indent, `snake_case` for the C++ members, comments that explain why.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-qt-selftest 2>&1 | tee /tmp/t1.log && QT_QPA_PLATFORM=offscreen ./build/cmake-final/hermes-qt-selftest 2>&1 | tail -3 && ! grep -q "warning:" /tmp/t1.log'`

Expected: all tests pass, including the six new ones, and the build log has no `warning:` line.

- [ ] **Step 5: Commit**

```bash
git add qtui/panel_tree.h qtui/panel_tree.cpp CMakeLists.txt tools/qtui-selftest.cpp
git commit -m "feat(ui): add the panel tree data model"
```

---

### Task 2: The controller owns the tree

**Files:**
- Modify: `qtui/shell_controller.h`, `qtui/shell_controller.cpp`
- Test: `tools/qtui-selftest.cpp`

**Interfaces:**
- Consumes: `PanelTree`, `Panel`, `PanelTab`, `PanelNode` from Task 1.
- Produces:

```cpp
// qtui/shell_controller.h
Q_PROPERTY(QVariantList panels READ panels NOTIFY panelsChanged)
Q_PROPERTY(int activePanel READ activePanel NOTIFY activePanelChanged)

QVariantList panels() const;   // one map per panel, see below
int activePanel() const;

Q_INVOKABLE void activatePanel(int panelId);
Q_INVOKABLE void selectTab(int panelId, int tabIndex);
Q_INVOKABLE void closeTab(int panelId, int tabIndex);
Q_INVOKABLE void newTabInPanel(int panelId);
Q_INVOKABLE void splitActiveRight();
Q_INVOKABLE void splitActiveDown();
Q_INVOKABLE void openInSplit(const QString &path, bool right);
Q_INVOKABLE void moveTabToSplit(int panelId, int tabIndex, bool right);
Q_INVOKABLE void cyclePanel(int direction);

// The size of the area the panels are laid out in; the QML reports it.
Q_INVOKABLE void setContentSize(int width, int height);

signals:
  void panelsChanged();
  void activePanelChanged();
```

Each entry of `panels()` is a `QVariantMap` with the keys `id` (the panel's index), `tabs` (a list of maps with `label` and `path`), `currentTab` (the index of the showing tab), `model` (the panel's `DirectoryModel *`), and `x`, `y`, `width`, `height` (the panel's rectangle).

The existing `tabs()`, `currentTab()`, `selectTab(int)`, `closeTab(int)` and `newTab(QString)` on the controller are replaced by the panel-aware ones above, because a tab now belongs to a panel. `goTo`, `back`, `forward`, `up` and the clipboard operations act on the active panel's current tab.

- [ ] **Step 1: Write the failing tests**

In `tools/qtui-selftest.cpp`:

- `controllerSplitsTheActivePanel`: a controller with one panel; `splitActiveRight()` gives two panels, the new one holds the same path as the old one, and the active panel is the new one.
- `controllerOpensAFolderInASplit`: `openInSplit(dir, true)` gives two panels and the new one shows `dir`.
- `controllerMovesATabIntoANewSplit`: a panel with two tabs; `moveTabToSplit(0, 0, true)` moves that tab into the new panel, so the source panel keeps one tab.
- `controllerRefusesToMoveTheLastTab`: a panel with one tab; `moveTabToSplit(0, 0, true)` does not empty the panel - the tab stays where it was and no new panel appears.
- `controllerClosesAPanelWithItsLastTab`: a panel with one tab; `closeTab(0, 0)` removes the panel and the tree has one panel left.
- `controllerRefusesASeventeenthPanel`: splitting until the limit; the next split leaves the panel count unchanged.
- `controllerNavigatesInTheActivePanelOnly`: two panels showing different directories; `navigate(other)` in the active panel does not change the other panel's path.
- `controllerKeepsTwoPanelsOfTheSameDirectoryIndependent`: two panels showing the same directory; navigating one of them leaves the other showing the same path.
- `controllerDropsTheOldScanWhenATabSwitches`: a panel whose tab is switched while its directory is still being scanned shows the new tab's directory, never the old tab's rows.
- `controllerKeepsTheRowsWhenAPanelDirectoryDisappears`: a panel whose directory is deleted while the window is open keeps its rows and does not crash.
- `controllerCyclesThePanels`: `cyclePanel(1)` activates the next panel in reading order and wraps around.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-qt-selftest 2>&1 | tail -5'`

Expected: the build fails, because the controller has no `splitActiveRight` and no `panels`.

- [ ] **Step 3: Implement the controller changes**

- Replace the controller's `QVector<TabState> m_tabs` with a `PanelTree m_tree`, and keep the history in each `PanelTab` instead of in a per-tab struct of the controller's own.
- Create one `DirectoryModel` per panel, parented to the controller, and one `DirectoryWatcher` per panel that watches the panel's current tab's path. When a panel's current tab changes, the model's path changes and the watcher follows.
- The active panel's current tab is where `navigate`, `back`, `forward`, `up`, the clipboard and the operations act.
- `splitActiveRight` and `splitActiveDown` split the active panel with `Zone::Right` and `Zone::Bottom` and open the active tab's path in the new panel; the new panel becomes active.
- `openInSplit` splits the active panel and opens the given path in the new panel.
- `moveTabToSplit` refuses when the panel would be left without a tab, mirroring the legacy's guard (`src/app.c:8399`).
- `closeTab` closes the tab; when it was the panel's last tab, the panel is closed and the sibling of its leaf takes the space.
- The commands are invokable so that the keybindings can be added later with the keys slice. This slice adds no shortcuts; the legacy's "Split panel right" and "Split panel down" come with that slice.
- `panels()` returns the map list described above, with the rectangles from `PanelTree::layout()` computed against the content size the controller is told about.

The content size: add `Q_INVOKABLE void setContentSize(int width, int height)`, which the QML calls when its content area changes; the controller recomputes the rectangles and emits `panelsChanged()`.

- [ ] **Step 4: Update the tests that used the old controller API**

`tools/qtui-selftest.cpp` has cases that use `hermes.tabs`, `hermes.currentTab`, `selectTab(int)` and `closeTab(int)`: `navigationNotifiesTabLabels`, `closeTabBeforeCurrentKeepsFocusedTab`, `focusFreeHostTabKeepsValidCursor`, `hostOpenTabAtIndexInsertsTab`, `selectionUsesPathListAndClearsOnNavigation`, `failedOperationRefreshPreservesSelection` and `hostHooksAreSafeAcrossControllerLifetime`. Update them to the panel-aware API - `selectTab(0, index)` and `closeTab(0, index)` - keeping what each case proves. A test that checks the tab labels now checks the labels of the active panel's tabs.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-qt hermes-qt-selftest 2>&1 | tee /tmp/t2.log && QT_QPA_PLATFORM=offscreen ./build/cmake-final/hermes-qt-selftest 2>&1 | tail -3 && ! grep -q "warning:" /tmp/t2.log'`

Expected: all tests pass and the build log has no `warning:` line.

- [ ] **Step 6: Commit**

```bash
git add qtui/shell_controller.h qtui/shell_controller.cpp tools/qtui-selftest.cpp
git commit -m "feat(ui): give the controller a tree of panels"
```

---

### Task 3: The window draws the panels

**Files:**
- Create: `qtui/qml/PanelView.qml`
- Modify: `qtui/qml/DirectoryPage.qml`, `qtui/qml/main.qml`
- Test: `tools/qtui-selftest.cpp`

**Interfaces:**
- Consumes: `hermes.panels`, `hermes.activePanel` and the panel-aware commands from Task 2.
- Produces: the QML objects the tests find by name.

- [ ] **Step 1: Write the failing tests**

In `tools/qtui-selftest.cpp`:

- `windowShowsOnePanelPerTreeNode`: load the QML, split the active panel right, and assert that two panel views exist. The panel views carry the object name `panelView_<id>`, so the test finds `panelView_0` and `panelView_1`.
- `eachPanelHasItsOwnTabStrip`: with two panels, each panel view contains a tab strip - the object name `panelTabs_<id>` - and the strips show different tabs.
- `aPanelViewShowsItsOwnDirectory`: two panels showing different directories; the directory view of each shows its own rows, found by the object name `directoryView_<id>`.
- `theToolbarActsOnTheActivePanel`: switching the view mode with the toolbar changes the active panel's view mode, and the other panel keeps its own.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-qt-selftest 2>&1 | tail -5'`

Expected: the tests fail, because there is one panel view and no per-panel strips.

- [ ] **Step 3: Implement the QML**

`qtui/qml/main.qml`: the shell page keeps the tab bar and the toolbar out of the panel area. The page's content holds a `Repeater` over `hermes.panels`, whose delegate is a `PanelView` placed at the panel's `x`, `y`, `width` and `height`. The page tells the controller its content size with `hermes.setContentSize(width, height)`.

`qtui/qml/PanelView.qml` (new): a `Column` with the panel's tab strip on top and the directory content below. The strip is a `Row` of `ToolButton`s over the panel's tabs, each with the object name `panelTab_<panelId>_<tabIndex>` and a close button `panelTabClose_<panelId>_<tabIndex>`, plus a new tab button `panelNewTab_<panelId>`. Clicking a tab calls `hermes.selectTab(panelId, tabIndex)`; the close button calls `hermes.closeTab(panelId, tabIndex)`; the new tab button calls `hermes.newTabInPanel(panelId)`.

`qtui/qml/DirectoryPage.qml` becomes the reusable directory content: an `Item` with the three views (list, details, grid), the size and date helpers and the keyboard handling, taking the model to show as a property. Its `objectName` becomes `directoryView`. The `Kirigami.Page` wrapper goes away, because a panel is not a page.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-qt hermes-qt-selftest 2>&1 | tee /tmp/t3.log && QT_QPA_PLATFORM=offscreen ./build/cmake-final/hermes-qt-selftest 2>&1 | tail -3 && ! grep -q "warning:" /tmp/t3.log'`

Expected: all tests pass and the build log has no `warning:` line.

- [ ] **Step 5: Commit**

```bash
git add qtui/qml/PanelView.qml qtui/qml/DirectoryPage.qml qtui/qml/main.qml tools/qtui-selftest.cpp
git commit -m "feat(ui): draw the panel tree in the window"
```

---

### Task 4: The state belongs to the tab, not to the view

**Files:**
- Modify: `qtui/directory_model.h`, `qtui/directory_model.cpp`, `qtui/shell_controller.h`, `qtui/shell_controller.cpp`, `qtui/qml/DirectoryPage.qml`
- Test: `tools/qtui-selftest.cpp`

**Interfaces:**
- Consumes: `PanelTab` from Task 1, the controller's panel-aware API from Task 2, the reusable directory content from Task 3.
- Produces:

```cpp
// qtui/shell_controller.h
Q_INVOKABLE QStringList selectedPaths() const;              // the active tab's
Q_INVOKABLE void setSelectedPaths(const QStringList &paths);
Q_INVOKABLE int currentIndex() const;                       // the active tab's
Q_INVOKABLE void setCurrentIndex(int index);
```

- [ ] **Step 1: Write the failing tests**

In `tools/qtui-selftest.cpp`:

- `selectionSurvivesATabSwitch`: select a row in a tab, switch to another tab and back, and assert the selection is the same set of paths.
- `selectionDoesNotLeakBetweenPanels`: select a row in one panel's tab and assert the other panel's tab has no selection.
- `closingAPanelDropsItsSelection`: close a panel that holds a selection and assert no panel has that selection any more.
- `theCursorSurvivesATabSwitch`: set the cursor on a row, switch tabs and back, and assert the cursor index is the same.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-qt-selftest 2>&1 | tail -5'`

Expected: the tests fail, because the selection lives in the QML view and is lost when the view is rebuilt.

- [ ] **Step 3: Implement the move**

The selection and the cursor move out of `DirectoryPage.qml`'s `property var selectedPaths` and `property int currentIndex` into the active tab's `PanelTab`. The QML reads and writes them through the controller's `selectedPaths`, `setSelectedPaths`, `currentIndex` and `setCurrentIndex`.

The selection is stored as the displayed paths, exactly as the view carries them today. The raw bytes stay the model's business: the operation boundary resolves a displayed path back to its bytes and refuses the operation when the mapping is ambiguous, as slice S1 settled it. Do not re-encode a path in the QML.

When a scan replaces the rows and a selected path is no longer among them, that path is dropped from the selection.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-qt hermes-qt-selftest 2>&1 | tee /tmp/t4.log && QT_QPA_PLATFORM=offscreen ./build/cmake-final/hermes-qt-selftest 2>&1 | tail -3 && ! grep -q "warning:" /tmp/t4.log'`

Expected: all tests pass and the build log has no `warning:` line.

- [ ] **Step 5: Commit**

```bash
git add qtui/directory_model.h qtui/directory_model.cpp qtui/shell_controller.h qtui/shell_controller.cpp qtui/qml/DirectoryPage.qml tools/qtui-selftest.cpp
git commit -m "feat(ui): keep the selection and the cursor in the tab state"
```

---

### Task 5: Documentation and changelog

**Files:**
- Modify: `docs/architecture.md`, `docs/building.md`, `CHANGELOG.md`

**Interfaces:**
- Consumes: what Tasks 1 to 4 produced.
- Produces: nothing that a later task consumes.

- [ ] **Step 1: Describe the panel tree in `docs/architecture.md`**

Add `qtui/panel_tree.*` to the "Qt/Kirigami shell" section: it holds the tree of panels, the tab on each panel and the operations on them. Update the paragraphs for `qtui/shell_controller.*` (it owns the tree, one model and one watcher per panel, and the panel-aware commands) and `qtui/directory_model.*` (one instance per panel now).

- [ ] **Step 2: Describe the behaviour in `docs/building.md`**

Say that the window holds a tree of panels, each with its own tab strip, that a panel can be split right or down, and that the layout is not yet persisted between runs - the session restore arrives with the slice's second pull request. Keep the note that the offscreen suites verify headless plumbing only and not live KDE Plasma behaviour.

- [ ] **Step 3: Add the changelog entry**

In `CHANGELOG.md`'s "Unreleased" section, add a bullet under "**New**" describing the user-visible result: the window can be split into panels, each with its own tabs.

- [ ] **Step 4: Verify the docs**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && python3 tools/build-install-selftest.py && python3 tools/branding-selftest.py && python3 tools/legacy-purge-selftest.py && sh tools/check-release-version.sh'`

Expected: all three Python suites pass and the release-version check reports the current version.

- [ ] **Step 5: Commit**

```bash
git add docs/architecture.md docs/building.md CHANGELOG.md
git commit -m "docs(ui): describe the panel tree"
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

## What is left to the builder

- The names of the QML files beyond `PanelView.qml`, if the split of `DirectoryPage.qml` needs another piece.
- Whether the panel list reaches QML as one property or as a model.
- The exact wording of the "Too many panels" message.
