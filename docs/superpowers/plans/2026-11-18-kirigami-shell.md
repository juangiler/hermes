# Kirigami Shell Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a first Kirigami file-manager shell (window, places, tabs, directory model, navigation and basic operations) over the preserved C engine, while the legacy SDL UI remains the default during migration.

**Architecture:** Extend the narrow C ABI with sorted scanning and engine-backed operations, then build a C++ `hermes-kirigami-ui` library that owns tabs/navigation and exposes typed Qt models to QML pages. A CMake option builds both the legacy SDL binary and `hermes-qt`; PR 5 removes that migration scaffolding.

**Tech Stack:** C11 unity core, C++17, Qt 6 (Core, Gui, Qml, Quick, QuickControls2, Test), KF6 Kirigami, ECM >= 6.0, CMake >= 3.20, Qt Test.

**Spec:** `docs/superpowers/specs/2026-11-18-kirigami-shell-design.md` (and the overall port design it references).

## Global Constraints

- Branch: `feat/ui/kirigami-shell` from `main` at `69aa32f`. PR-only `main`; atomic conventional commits; no merge/push/tag without explicit approval.
- KDE Plasma 6.6-6.8 is the target; headless runs verify builds/tests only, not live Plasma behaviour.
- Runtime UI must use Qt Quick's hardware scene graph. No `Canvas`, `QPainter`, `QGraphicsView`, or forced software backend in shipped code; offscreen software rendering is test-only.
- C engine sources (`src/*.c`, `src/*.h`) stay pure ASCII and warning-clean; UTF-8 literals use `\x` escapes.
- No frontend reimplementation of engine logic: scanning, sorting, operations, conflict and cancellation policy live in C.
- Portal roles stay opt-in; PR 3 does not change `portals/` wire behaviour or add a picker window.
- Keep every existing CI check green: `source-checks`, `package-versions`, `selftest`, four `build` matrix legs, `core-selftest`, `portal-selftest`. New checks must not replace required ones.
- No dependency installation without approval. `fedora-build` already has ECM, `KF6Kirigami`, Qt Quick/QML/Test available through CMake; use `distrobox enter fedora-build -- ...` for local CMake verification.

## Review Focus

- Non-ASCII and very long filenames: directory model and operations must preserve full UTF-8 names and not truncate; `directoryModelPreservesUtf8Names` and `operationsHandleUtf8Names` cover this.
- Unreadable directories and vanished paths: scans must fail cleanly without changing the current directory; `directoryModelFailedNavigateKeepsPath` covers this.
- Existing destination on copy/move: the headless shell must never block on SDL's `CF_ASK` conflict dialog; `operationsUseConfiguredConflictPolicy` covers this.
- Stale scan callbacks during rapid navigation: an old scan must not repaint the new directory; `navigationIgnoresStaleDirectoryResults` covers this.
- Missing QML modules at runtime: a broken resource/import must fail loudly in tests, not render a blank window; `qmlMainLoadsOffscreen` covers this.

---

### Task 1: C ABI for sorted scans and engine operations

**Files:**
- Modify: `src/core_api.h`, `src/core_api.c`, `src/core_host.h`, `src/hermes_core.c`
- Modify: `src/app.c` (SDL host table initializer only), all other `HermesCoreHost` aggregate initializers found by `grep -R "HermesCoreHost" src tools portals`
- Test: `tools/core-abi-selftest.c`, `tools/core-api-probe.cpp` (compile check only)

**Interfaces:**
- Consumes: `hermes_core_scan` / `hermes_listing_free` (`src/core_api.h:39-40`), `listing_build_order_ex` and `OrderOpts` (`src/fs.c:287-295,379-397`), `op_new` / `op_launch` / engine kinds and policies (`src/ops.c:9-13,88-98,1109`), `op_finished` in the core unity source (`src/hermes_core.c:36`).
- Produces: new C ABI used by Tasks 3-5:

```c
typedef enum {
    HERMES_SORT_NAME = 0, HERMES_SORT_DATE = 1,
    HERMES_SORT_TYPE = 2, HERMES_SORT_SIZE = 3
} HermesSortKey;

typedef enum {
    HERMES_CONFLICT_SKIP = 0, HERMES_CONFLICT_REPLACE = 1,
    HERMES_CONFLICT_RENAME = 2, HERMES_CONFLICT_KEEP_BOTH = 3,
    HERMES_CONFLICT_NEWER = 4
} HermesConflictPolicy;

typedef enum {
    HERMES_OP_MKDIR = 0, HERMES_OP_RENAME = 1, HERMES_OP_TRASH = 2,
    HERMES_OP_DELETE = 3, HERMES_OP_COPY = 4, HERMES_OP_MOVE = 5
} HermesOpKind;

enum { HERMES_ENTRY_DIR = 1, HERMES_ENTRY_LINK = 2, HERMES_ENTRY_HIDDEN = 4 };

int hermes_core_scan_sorted(HermesCore *core, const char *path,
                            HermesSortKey key, bool desc, HermesListing *out);
int hermes_core_mkdir(HermesCore *core, const char *parent, const char *name);
int hermes_core_rename(HermesCore *core, const char *from, const char *to);
int hermes_core_remove(HermesCore *core, const char *const *paths,
                       size_t count, bool trash);
int hermes_core_copy(HermesCore *core, const char *const *paths, size_t count,
                     const char *dest_dir, bool move, HermesConflictPolicy policy);
```

  and one new final field in `HermesCoreHost` (`src/core_host.h:12-20`):

```c
void (*operations_changed)(int kind, int error_count); // kind = HermesOpKind
```

  All operation functions return `0` when a job is accepted, `-1` for invalid input; completion is reported on the GUI thread by `operations_changed` after the engine's `main_post` callback runs. Map `HermesConflictPolicy` explicitly to engine `CF_SKIP`, `CF_REPLACE`, `CF_RENAME`, `CF_KEEP_BOTH`, `CF_NEWER`; never leave `CF_ASK` set, because no headless answerer exists.

- [ ] **Step 1: Write failing ABI tests**

Add to `tools/core-abi-selftest.c`, using its existing temp-directory/cleanup helpers and a host table that records `operations_changed` via a global counter:

- `test_scan_sorted`: create `a.txt`, `b.txt`, `zdir` in a temp dir; call `hermes_core_scan_sorted(core, dir, HERMES_SORT_NAME, false, &listing)`. Assert `listing.count == 3`, the first item has `flags & HERMES_ENTRY_DIR`, and remaining names are `a.txt`, `b.txt`. Call again with `HERMES_SORT_SIZE, true` after giving `a.txt` more bytes and assert `a.txt` is first.
- `test_ops_mkdir_rename_remove`: call `hermes_core_mkdir`, wait for `operations_changed(HERMES_OP_MKDIR, 0)` after `hermes_core_drain(core)`, assert the directory exists; rename it with `hermes_core_rename`, assert the new path exists; remove it with `trash=false`, assert it is gone.
- `test_ops_copy_move_conflict_policy`: copy a file into a destination that already has the same name using `HERMES_CONFLICT_RENAME`; assert the call returns `0`, completion arrives, and a renamed copy exists without overwriting the original. Repeat with `move=true` into another directory.
- `test_operations_changed_hook`: assert the recorded kind/error_count matches the completed op and that no completion is reported for invalid input.

Extend `tools/core-api-probe.cpp` with calls that take the address of each new function so the C++ header linkage is checked.

- [ ] **Step 2: Run tests to verify they fail**

Run:

```sh
distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake -S . -B build/cmake && cmake --build build/cmake --target hermes-core-abi-selftest hermes-cpp-probe && ./build/cmake/hermes-core-abi-selftest'
```

Expected: compilation fails on undeclared ABI functions/fields.

- [ ] **Step 3: Implement the C ABI**

In `src/core_api.h`, add the three enums, five functions and the new host field. In `src/core_host.h`, append `operations_changed` to `HermesCoreHost` and its default table as a no-op. Update every aggregate initializer found in Step 1 so all seven existing fields keep their order and the eighth is a no-op or recorder.

In `src/core_api.c`:

- Refactor `hermes_core_scan` to call `hermes_core_scan_sorted(core, path, HERMES_SORT_NAME, false, out)`; keep its current validation, wait loop and `hermes_listing_free` contract.
- Implement `hermes_core_scan_sorted` with the same wait loop, then call `listing_build_order_ex(L, &(OrderOpts){key, desc, true, true, NULL, 0, 0, NULL, false}, &order, &n)` and copy entries in `order[i]` sequence into `out->items`. Free `order` and release `L` on every path.
- Add a private `launch_op(core, kind, src, nsrc, dst_dir, dst, policy)` helper: validate the live core, non-empty path array, `count <= INT_MAX`, and non-null/non-empty strings; call `op_new`, set `op->dst` when provided, set `op->policy` from the explicit mapping, then `op_launch(op)`. For `hermes_core_rename`, pass `from` as `src[0]` and allocate `op->dst[0] = strdup(to)`. For `hermes_core_mkdir`, pass the joined `parent/name` path as the single source. For `hermes_core_copy`, allocate `dst_dir` as the engine destination and set `move ? OP_MOVE : OP_COPY`. For `hermes_core_remove`, use `OP_TRASH` or `OP_DELETE`. Operations are asynchronous and `queued` stays false.

In `src/hermes_core.c`, change the local `op_finished` stub so it reads `op->kind` and `op->nerr`, calls `g_core_host->operations_changed(kind, nerr)`, then calls `op_free(op)`. Keep the callback on the main-thread callback path; do not call it from the worker thread.

- [ ] **Step 4: Run ABI, core and legacy checks**

Run:

```sh
distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake --target hermes-core-selftest hermes-core-abi-selftest hermes-cpp-probe && ./build/cmake/hermes-core-selftest && ./build/cmake/hermes-core-abi-selftest && ./build/cmake/hermes-cpp-probe'
./build.sh package
LC_ALL=C grep -nP '[^\x00-\x7F]' src/*.c src/*.h
```

Expected: all suites pass, legacy build has no warnings, ASCII check prints nothing.

- [ ] **Step 5: Commit**

```sh
git add src/core_api.h src/core_api.c src/core_host.h src/hermes_core.c src/app.c tools/core-abi-selftest.c tools/core-api-probe.cpp
git commit -m "feat(core): expose sorted scans and engine operations"
```

### Task 2: Kirigami CMake scaffold and offscreen QML load test

**Files:**
- Modify: `CMakeLists.txt`
- Create: `qtui/main.cpp`, `qtui/qml/main.qml`, `qtui/hermes-qt.qrc`, `tools/qtui-selftest.cpp`
- Test: `tools/qtui-selftest.cpp` (initially only `qmlMainLoadsOffscreen`)

**Interfaces:**
- Consumes: `hermes_core` / `hermes-portals` CMake targets, `hermes_core_create(HermesWakeFn, void *)`, and all seven original host hooks.
- Produces: `hermes-kirigami-ui` STATIC library (AUTOMOC), `hermes-qt` executable, `hermes-qt-selftest` Qt Test executable, and the QML resource path `qrc:/qtui/qml/main.qml`. Later tasks add files to the library and tests to the same binary.

- [ ] **Step 1: Write the failing QML load test**

Create `tools/qtui-selftest.cpp` with a `QObject` test class, `QTEST_MAIN`, `#include "qtui-selftest.moc"`, `AUTOMOC ON`, and slot `qmlMainLoadsOffscreen()`:

```cpp
QQmlApplicationEngine engine;
engine.load(QUrl(QStringLiteral("qrc:/qtui/qml/main.qml")));
QVERIFY(!engine.rootObjects().isEmpty());
```

In `initTestCase()`, set `XDG_CONFIG_HOME` to a `QTemporaryDir` path. Do not set `QT_QPA_PLATFORM` in code; the runner supplies `offscreen`.

- [ ] **Step 2: Run the test to verify it fails**

```sh
distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake -S . -B build/cmake-ui -DHERMES_BUILD_QT_UI=ON && cmake --build build/cmake-ui --target hermes-qt-selftest && QT_QPA_PLATFORM=offscreen ./build/cmake-ui/hermes-qt-selftest'
```

Expected: CMake fails because the target and QML resource do not exist.

- [ ] **Step 3: Add CMake targets and minimal QML**

In `CMakeLists.txt`:

- Add `option(HERMES_BUILD_QT_UI "Build the Kirigami frontend" ON)` and `set(HERMES_DEFAULT_UI "legacy" CACHE STRING "Default installed UI during migration")` with allowed values `legacy` and `kirigami`; `message(FATAL_ERROR ...)` for any other value, and `set_property(CACHE HERMES_DEFAULT_UI PROPERTY STRINGS legacy kirigami)`.
- Inside `if(HERMES_BUILD_QT_UI)`: `find_package(ECM 6.0 REQUIRED NO_MODULE)`, append `${ECM_MODULE_PATH}` to `CMAKE_MODULE_PATH`, `find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Test)`, `find_package(KF6Kirigami REQUIRED)`, `include(GNUInstallDirs)`.
- Add `hermes-kirigami-ui` STATIC with `AUTOMOC ON`, `PUBLIC` include dir `qtui`, and `PUBLIC` links to `Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 KF6::Kirigami hermes_core hermes-portals`. Use C++17 and `-Wall -Wextra -Werror` on this target.
- Use `qt_add_resources(hermes-kirigami-ui "hermes-qml" PREFIX "/qtui" FILES qtui/qml/main.qml)`; add more QML files here in later tasks.
- Add `hermes-qt` executable from `qtui/main.cpp`, linked to `hermes-kirigami-ui`, output name `hermes-qt` in `${CMAKE_BINARY_DIR}`.
- Add `hermes-qt-selftest` from `tools/qtui-selftest.cpp`, `AUTOMOC ON`, `-Wall -Wextra -Werror`, linked to `hermes-kirigami-ui` and `Qt6::Test`, output in `${CMAKE_BINARY_DIR}`.
- Add `hermes-legacy` executable from `src/main.c` with the build.sh flags (`-std=gnu11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-format-truncation`) and links to SDL/pkg-config modules `sdl3 freetype2 harfbuzz fontconfig libvterm` plus `PkgConfig::CORE m dl pthread`; output name `hermes-legacy`.
- Install both binaries under `${CMAKE_INSTALL_BINDIR}`; install `hermes-legacy` as `hermes` when `HERMES_DEFAULT_UI=legacy`, otherwise install `hermes-qt` as `hermes`. This install alias is migration-only and is deleted in PR 5.

`qtui/main.cpp`: create `QGuiApplication`, a `QQmlApplicationEngine`, expose a placeholder context property `hermes` as a `QObject*`, load `qrc:/qtui/qml/main.qml`, and return a non-zero exit code with a `qCritical` message if `rootObjects()` is empty. Accept `--version` and `--help` with short text; pass positional paths to the controller in Task 4.

`qtui/qml/main.qml`: a minimal `Kirigami.ApplicationWindow` with `title: "Hermes"`, `pageStack.initialPage: Kirigami.Page { title: "Hermes" }`, and versionless imports (`import QtQuick`, `import QtQuick.Controls`, `import org.kde.kirigami as Kirigami`). No `Canvas`.

- [ ] **Step 4: Verify scaffold and legacy build**

```sh
distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake -S . -B build/cmake-ui -DHERMES_BUILD_QT_UI=ON && cmake --build build/cmake-ui --target hermes-legacy hermes-qt hermes-qt-selftest 2>&1 | tee /tmp/hermes-cmake-build.log && ! grep -q "warning:" /tmp/hermes-cmake-build.log && QT_QPA_PLATFORM=offscreen ./build/cmake-ui/hermes-qt-selftest'
```

Expected: the QML test passes; no compiler warnings.

- [ ] **Step 5: Commit**

```sh
git add CMakeLists.txt qtui tools/qtui-selftest.cpp
git commit -m "feat(ui): scaffold Kirigami shell and offscreen QML test"
```

### Task 3: Directory model over the sorted C ABI

**Files:**
- Create: `qtui/directory_model.h`, `qtui/directory_model.cpp`
- Modify: `qtui/qml/main.qml`, create `qtui/qml/DirectoryPage.qml`
- Modify: `CMakeLists.txt` (add sources/QML), `tools/qtui-selftest.cpp`
- Test: `tools/qtui-selftest.cpp`

**Interfaces:**
- Consumes: `hermes_core_scan_sorted`, `hermes_listing_free`, `HermesSortKey`, `HermesEntry.flags` (`HERMES_ENTRY_DIR=1`, `HERMES_ENTRY_HIDDEN=4`).
- Produces:

```cpp
class DirectoryModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { NameRole = Qt::UserRole + 1, PathRole, FlagsRole,
                SizeRole, ModifiedRole, IsDirRole };
    Q_ENUM(Role)
    explicit DirectoryModel(HermesCore *core, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QString path() const;
    bool navigate(const QString &path);
    void setSort(HermesSortKey key, bool desc);
public slots:
    void refresh();
signals:
    void pathChanged(const QString &path);
    void navigateFailed(const QString &path);
};
```

  Register `DirectoryModel` with `qmlRegisterType` or expose it as a Q_PROPERTY on the controller in Task 4. Roles in QML: `model.name`, `model.path`, `model.size`, `model.modified`, `model.isDir`.

- [ ] **Step 1: Write failing model tests**

Add slots to `tools/qtui-selftest.cpp`:

- `directoryModelListsSortedEntries`: temp dir with `a.txt`, `b.txt`, `zdir`; construct `HermesCore` with a wake callback that does nothing, call `navigate(dir)`; assert `rowCount() == 3`, `data(index(0), IsDirRole).toBool()`, and names `a.txt`, `b.txt` afterwards. Call `setSort(HERMES_SORT_NAME, true)` and `refresh()`; assert `zdir` is still first and `b.txt` precedes `a.txt`.
- `directoryModelReportsRoles`: assert `NameRole`/`PathRole`/`SizeRole`/`ModifiedRole`/`IsDirRole` return the values set up in the fixture and that `path()` equals the navigated path.
- `directoryModelPreservesUtf8Names`: create a filename containing `caf\xc3\xa9` (build the QString with `QString::fromUtf8`) and assert the exact name is returned by `NameRole` and `PathRole`.
- `directoryModelFailedNavigateKeepsPath`: navigate to a nonexistent path; assert `navigate()` returns false, `navigateFailed` is emitted with that path, and `path()`/rows remain unchanged.

- [ ] **Step 2: Run tests to verify they fail**

```sh
distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-ui --target hermes-qt-selftest && QT_QPA_PLATFORM=offscreen ./build/cmake-ui/hermes-qt-selftest directoryModelListsSortedEntries'
```

Expected: compile/link failure on missing `DirectoryModel`.

- [ ] **Step 3: Implement the model**

`navigate()` calls `hermes_core_scan_sorted(m_core, path.toUtf8().constData(), m_key, m_desc, &listing)`; on success it calls `beginResetModel()`, replaces the cached `QVector` of `{name, path, flags, size, mtime}` entries, sets `m_path`, calls `endResetModel()`, and emits `pathChanged`. On failure it frees the listing, emits `navigateFailed`, and returns false without touching rows. `refresh()` re-scans `m_path`. `setSort()` stores the key/direction and calls `refresh()` only if a path is loaded. `data()` returns a `QVariant` for the six roles; `IsDirRole` is `flags & HERMES_ENTRY_DIR`. `roleNames()` maps the enum to the QML names above. All strings are constructed from `HermesEntry.name` with `QString::fromUtf8(name, name_len)`.

`DirectoryPage.qml`: a `Kirigami.Page` containing a `ListView` with `model: hermes.directory`, `onCurrentItemChanged` not required, and a delegate using `Kirigami.Icon` plus `Text` bound to `model.name` and `model.size`; double-click emits `hermes.openEntry(model.path)` (implemented in Task 4). For now, wire `main.qml` to the page and expose a model instance through the placeholder context property so `qmlMainLoadsOffscreen` still passes.

- [ ] **Step 4: Run the model and QML tests**

```sh
distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-ui --target hermes-qt-selftest && QT_QPA_PLATFORM=offscreen ./build/cmake-ui/hermes-qt-selftest'
```

Expected: all current Qt tests pass.

- [ ] **Step 5: Commit**

```sh
git add qtui/directory_model.h qtui/directory_model.cpp qtui/qml/DirectoryPage.qml qtui/qml/main.qml CMakeLists.txt tools/qtui-selftest.cpp
git commit -m "feat(ui): add engine-backed directory model"
```

### Task 4: Tabs, places, navigation and host hooks

**Files:**
- Create: `qtui/shell_controller.h`, `qtui/shell_controller.cpp`, `qtui/places_model.h`, `qtui/places_model.cpp`
- Modify: `qtui/main.cpp`, `qtui/qml/main.qml`, `qtui/qml/DirectoryPage.qml`, `CMakeLists.txt`, `tools/qtui-selftest.cpp`
- Test: `tools/qtui-selftest.cpp`

**Interfaces:**
- Consumes: `DirectoryModel` from Task 3, `hermes_core_create/destroy/drain`, `hermes_core_open_uris` (`src/core_api.h:38`), `OpenUriClient::openFile(const QString &, const QString &)` (`portals/open_uri_client.h:38`), host table (`src/core_host.h:12-20`).
- Produces:

```cpp
class PlacesModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { LabelRole = Qt::UserRole + 1, PathRole };
    explicit PlacesModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
};

class ShellController : public QObject {
    Q_OBJECT
    Q_PROPERTY(DirectoryModel *directory READ directory CONSTANT)
    Q_PROPERTY(PlacesModel *places READ places CONSTANT)
    Q_PROPERTY(QStringList tabs READ tabs NOTIFY tabsChanged)
    Q_PROPERTY(int currentTab READ currentTab NOTIFY currentTabChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY historyChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY historyChanged)
public:
    explicit ShellController(QObject *parent = nullptr);
    ~ShellController() override;
    DirectoryModel *directory() const;
    PlacesModel *places() const;
    QStringList tabs() const;
    int currentTab() const;
    bool canGoBack() const;
    bool canGoForward() const;
    HermesCore *core() const;
public slots:
    void setPaths(const QStringList &paths);
    void navigate(const QString &path);
    void openEntry(const QString &path);
    void newTab(const QString &path);
    void closeTab(int index);
    void selectTab(int index);
    void up();
    void back();
    void forward();
signals:
    void tabsChanged();
    void currentTabChanged();
    void historyChanged();
};
```

  `PlacesModel` rows are the PR-3 subset: Home (`QStandardPaths::homeLocation`), existing XDG user dirs (Desktop, Documents, Downloads, Pictures, Music, Videos), Trash (`$XDG_DATA_HOME/Trash/files` or `~/.local/share/Trash/files`) and File System (`/`). Device/network/cloud panels stay for PR 4.

  A file-scope `static ShellController *g_controller` holds the active controller. Static C hooks in `shell_controller.cpp` forward to it with `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` where a GUI-thread effect is needed; `current_tab` returns `g_controller ? g_controller->currentTab() : 0`. `main.cpp` installs the table with `hermes_core_set_host(&table)` before creating any core, and resets it to `nullptr` after destroying the controller. All seven original hooks are non-null: `is_selftest`, `is_screenshot`, `window_hidden`, `mark_settings_dirty`, `request_redraw`, `open_tab`, `current_tab`.

- [ ] **Step 1: Write failing navigation/host tests**

Add slots:

- `hostOpenUrisCreatesTabs`: construct `ShellController`, call `hermes_core_open_uris(controller->core(), uris, 2, true)` with two `file://` URIs; use `QTRY_COMPARE(controller->tabs().size(), 2)` and assert both paths are present.
- `navigationHistoryBackForward`: navigate `dir`, `dir/child`, then `back()`; assert `directory()->path()` is `dir`, `canGoForward()` true, then `forward()` returns to `dir/child`.
- `upAndFailedPathKeepState`: navigate `dir/child`, call `up()`, assert `dir`; navigate a nonexistent path and assert the current path is unchanged.
- `placesIncludeHomeAndRoot`: assert `PlacesModel` rows include `QStandardPaths::homeLocation()` and `/`.
- `navigationIgnoresStaleDirectoryResults`: navigate twice in quick succession to two dirs with different entry counts; drain the core and wait for the model; assert the final rows belong only to the second directory. Implement by having `DirectoryModel::refresh()` compare the requested path against `m_path` before applying results (add this guard in Step 3).

- [ ] **Step 2: Run tests to verify they fail**

```sh
distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-ui --target hermes-qt-selftest && QT_QPA_PLATFORM=offscreen ./build/cmake-ui/hermes-qt-selftest hostOpenUrisCreatesTabs'
```

Expected: compile/link failure on missing `ShellController`/`PlacesModel`.

- [ ] **Step 3: Implement controller, places and host hooks**

`ShellController` owns one `HermesCore` created with a wake callback that calls `QMetaObject::invokeMethod(this, []{ if (m_directory) m_directory->refresh(); }, Qt::QueuedConnection)`; it calls `hermes_core_drain(core)` when processing the queued refresh. Tabs are a `QVector<TabState>` where `TabState` has `QString path`, `QStringList history`, and `int historyIndex`. `newTab(path)` appends (or focuses when `index >= 0` via host `open_tab`), `closeTab(index)` removes, `selectTab(index)` changes focus and loads that tab's path. `navigate(path)` replaces the forward history. `openEntry(path)` navigates for directories and calls `OpenUriClient::openFile(path, QString())` for files. Destruction destroys the core and clears `g_controller` before resetting the host table.

Add the stale-result guard in `DirectoryModel`: `refresh()` captures `m_path` and `navigate()` increments a generation counter; applying a scan result is skipped if the generation changed while the scan ran.

`main.cpp` replaces the placeholder context property with the `ShellController` instance, passes positional arguments to `setPaths()`, and keeps the `--version`/`--help` handling from Task 2.

`main.qml`: add a `TabBar` bound to `hermes.tabs` with `TabButton` calling `hermes.selectTab(index)`, a `header` toolbar with Up/Back/Forward buttons disabled from `hermes.canGoBack`/`canGoForward`, and a `Kirigami.GlobalDrawer` whose `ListView` uses `hermes.places` and calls `hermes.navigate(model.path)`. `DirectoryPage.qml`: double-click delegate calls `hermes.openEntry(model.path)`.

- [ ] **Step 4: Run Qt, core and portal suites**

```sh
distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-ui --target hermes-qt-selftest && QT_QPA_PLATFORM=offscreen ./build/cmake-ui/hermes-qt-selftest && cmake --build build/cmake-ui --target hermes-core-abi-selftest && ./build/cmake-ui/hermes-core-abi-selftest && sh tools/portal-selftest.sh'
```

Expected: all three suites pass; no real desktop config is changed.

- [ ] **Step 5: Commit**

```sh
git add qtui CMakeLists.txt tools/qtui-selftest.cpp
git commit -m "feat(ui): add tabs, places and navigation controller"
```

### Task 5: Basic operations in the shell

**Files:**
- Modify: `qtui/shell_controller.h`, `qtui/shell_controller.cpp`, `qtui/qml/DirectoryPage.qml`, `qtui/qml/main.qml`, `tools/qtui-selftest.cpp`
- Test: `tools/qtui-selftest.cpp`

**Interfaces:**
- Consumes: Task 1 C ABI operations and `operations_changed` hook; Task 4 `ShellController::core()`/`directory()`.
- Produces on `ShellController`:

```cpp
Q_INVOKABLE bool createFolder(const QString &name);
Q_INVOKABLE bool renameEntry(const QString &from, const QString &to);
Q_INVOKABLE bool removeEntries(const QStringList &paths, bool trash);
Q_INVOKABLE bool copyEntries(const QStringList &paths, const QString &destDir, bool move);
Q_INVOKABLE void copySelection(const QStringList &paths, bool move);
Q_INVOKABLE void pasteIntoCurrentDirectory();
Q_INVOKABLE void setConflictPolicy(HermesConflictPolicy policy);
```

  Default policy is `HERMES_CONFLICT_RENAME` (never overwrite silently). Add the `operations_changed` field to the host table in `main.cpp` so it invokes `ShellController::refreshDirectory()` on the GUI thread.

- [ ] **Step 1: Write failing operation tests**

Add slots:

- `operationsCreateRenameAndTrash`: temp dir path in `controller->directory()`, call `createFolder("folder")`, wait for directory refresh, assert it appears; `renameEntry(old, new)`, assert new name appears; `removeEntries({new}, true)`, assert it disappears from the listing.
- `operationsHandleUtf8Names`: repeat create/rename with a `caf\xc3\xa9` name built via `QString::fromUtf8` and assert the exact name appears.
- `operationsUseConfiguredConflictPolicy`: create `dest/existing.txt` and `src/existing.txt`, call `copyEntries` twice with `HERMES_CONFLICT_RENAME`; assert both calls return quickly (use `QTRY_VERIFY` on completion rather than a blocking wait) and that the destination now has two distinct files with the original content unchanged.
- `copyPasteMoveRoundTrip`: `copySelection({filePath}, false)` on a file, `navigate(dest)`, `pasteIntoCurrentDirectory()`, assert the file exists there; repeat with `copySelection({filePath}, true)` and assert the source is gone.

For all tests, clear the recorded `operations_changed` counter from Task 1 after each op and use `QTRY_COMPARE` with a 2000 ms timeout.

- [ ] **Step 2: Run tests to verify they fail**

```sh
distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-ui --target hermes-qt-selftest && QT_QPA_PLATFORM=offscreen ./build/cmake-ui/hermes-qt-selftest operationsCreateRenameAndTrash'
```

Expected: compile failure on missing invokables.

- [ ] **Step 3: Implement operation methods and QML actions**

Each method builds UTF-8 `QByteArray` copies of its arguments (they must outlive the call), calls the matching `hermes_core_*` function, and returns its result. `copySelection(paths, move)` stores the supplied paths in the controller clipboard; `DirectoryPage.qml` keeps a `set: string` selection set and calls `hermes.copySelection(selectedPaths, move)` from toolbar actions. `pasteIntoCurrentDirectory()` uses the recorded clipboard and `directory()->path()`. `refreshDirectory()` (invoked from `operations_changed`) calls `directory()->refresh()`.

`main.qml`/`DirectoryPage.qml`: add toolbar `Kirigami.Action`s for New folder, Rename, Copy, Cut, Paste, Trash and Delete. New folder and Rename use a small `Kirigami.Dialog` with a `TextField`; destructive actions call `removeEntries(paths, trash)` directly. No `Canvas` and no `QPainter`.

- [ ] **Step 4: Run full local verification**

```sh
distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-ui --target hermes-legacy hermes-qt hermes-qt-selftest 2>&1 | tee /tmp/hermes-ui-build.log && ! grep -q "warning:" /tmp/hermes-ui-build.log && QT_QPA_PLATFORM=offscreen ./build/cmake-ui/hermes-qt-selftest && cmake --build build/cmake-ui --target hermes-core-selftest hermes-core-abi-selftest hermes-cpp-probe && ./build/cmake-ui/hermes-core-selftest && ./build/cmake-ui/hermes-core-abi-selftest && ./build/cmake-ui/hermes-cpp-probe'
./build.sh debug && ./build/debug/hermes --selftest-ops
python3 tools/build-install-selftest.py && python3 tools/branding-selftest.py && python3 tools/legacy-purge-selftest.py
LC_ALL=C grep -nP '[^\x00-\x7F]' src/*.c src/*.h
git diff --check
```

Expected: all suites pass, no warnings, ASCII and whitespace checks clean.

- [ ] **Step 5: Commit**

```sh
git add qtui tools/qtui-selftest.cpp
git commit -m "feat(ui): wire basic file operations into the shell"
```

### Task 6: CI, documentation and migration install flag

**Files:**
- Modify: `.github/workflows/ci.yml`, `docs/building.md`, `docs/architecture.md`, `CHANGELOG.md`, `docs/superpowers/specs/2026-11-18-kirigami-shell-design.md` (status only if needed)
- Test: CI commands in `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: all targets from Tasks 1-5.
- Produces: the same seven required check contexts, with the four `build` matrix legs additionally building and testing both UIs.

- [ ] **Step 1: Add packages and CMake steps to the build matrix**

In `.github/workflows/ci.yml`, append to the four install commands:

- `archlinux*`: `qt6-base qt6-declarative kirigami extra-cmake-modules qqc2-desktop-style`
- `fedora*`: `qt6-qtbase-devel qt6-qtdeclarative-devel kf6-kirigami-devel extra-cmake-modules kf6-qqc2-desktop-style`
- `debian*|ubuntu*`: `qt6-base-dev qt6-declarative-dev libkirigami-dev extra-cmake-modules qml6-module-org-kde-kirigami qml6-module-org-kde-desktop`

Keep the existing `./build.sh package` step and warning check unchanged. Add a CMake step after it:

```sh
cmake -S . -B build/cmake-ui -DHERMES_BUILD_QT_UI=ON -DHERMES_DEFAULT_UI=legacy
cmake --build build/cmake-ui --target hermes-legacy hermes-qt hermes-qt-selftest 2>&1 | tee cmake-ui-build.log
if grep -q 'warning:' cmake-ui-build.log; then echo "::error::Qt UI compiler warnings"; exit 1; fi
QT_QPA_PLATFORM=offscreen ./build/cmake-ui/hermes-qt-selftest
cmake -S . -B build/cmake-ui-qt -DHERMES_BUILD_QT_UI=ON -DHERMES_DEFAULT_UI=kirigami
cmake --build build/cmake-ui-qt --target hermes-qt
```

Core and portal jobs pass `-DHERMES_BUILD_QT_UI=OFF` when configuring so they keep their existing package sets. No new required check names are introduced.

- [ ] **Step 2: Verify CI YAML and all required checks locally**

```sh
python3 - <<'PY'
import yaml
with open('.github/workflows/ci.yml', encoding='utf-8') as f:
    yaml.safe_load(f)
print('ci.yml ok')
PY
python3 tools/build-install-selftest.py
python3 tools/branding-selftest.py
python3 tools/legacy-purge-selftest.py
git diff --exit-code
```

Expected: YAML parses, Python suites pass, working tree has no unexpected generated changes.

- [ ] **Step 3: Document the new UI and verification commands**

In `docs/building.md`, add a Qt/Kirigami build section with the four distro package lists from Step 1, the CMake commands above, and the note that offscreen Qt Test runs do not prove live Plasma behaviour. In `docs/architecture.md`, add a `qtui/` section describing `main.cpp`, `shell_controller.*`, `directory_model.*`, `places_model.*` and `qml/` ownership, and update the repository layout list. In `CHANGELOG.md` add Unreleased bullets for the Kirigami shell, sorted-scan/ops C ABI and migration install flag. State in docs that `HERMES_DEFAULT_UI` is temporary and removed with the SDL UI in PR 5, and that Qt equivalents of `--screenshot`/`HERMES_RECORD` are deferred to PR 4.

- [ ] **Step 4: Final whole-branch verification**

```sh
distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake -S . -B build/cmake-final -DHERMES_BUILD_QT_UI=ON -DHERMES_DEFAULT_UI=legacy && cmake --build build/cmake-final --target hermes-legacy hermes-qt hermes-qt-selftest hermes-core-selftest hermes-core-abi-selftest hermes-cpp-probe hermes-portal-selftest && QT_QPA_PLATFORM=offscreen ./build/cmake-final/hermes-qt-selftest && ./build/cmake-final/hermes-core-selftest && ./build/cmake-final/hermes-core-abi-selftest && ./build/cmake-final/hermes-cpp-probe && sh tools/portal-selftest.sh'
./build.sh debug
./build/debug/hermes --selftest-ops
git diff --check
```

Expected: everything passes; no modified/untracked files except the intended plan/design docs and the pre-existing `docs/verified-commits.md`.

- [ ] **Step 5: Commit**

```sh
git add .github/workflows/ci.yml docs/building.md docs/architecture.md CHANGELOG.md docs/superpowers/specs/2026-11-18-kirigami-shell-design.md
git commit -m "build(ui): build both UIs in CI and document migration"
```
