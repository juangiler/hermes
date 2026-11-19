# Asynchronous scanning and directory view parity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Kirigami frontend scan directories off the GUI thread and give the directory view the legacy application's view modes, columns, sorting, hidden-file handling, folder sizes, selection and keyboard navigation.

**Architecture:** The C engine already scans on worker threads (`listing_start` submits `scan_job` to the pool; metadata runs in `meta_job` workers; the listing state moves `LST_READY` -> `LST_COMPLETE`). The blocking is entirely in the ABI, which polls that state in a sleep loop (`src/core_api.c:91-97`). This plan exposes the engine's asynchronous scan through the C ABI as a start/poll/cancel/free handle, lets the Qt model poll it from the GUI thread, and keeps every engine decision (hidden entries, sort key, folder sizes) in C.

**Tech Stack:** C11 (engine, `extern "C"` ABI), C++17 + Qt 6 (models/controllers), QML + Kirigami (views), CMake, Qt Test, GoogleTest-free plain assertions in the C suites.

**Spec:** `docs/superpowers/specs/2026-11-19-kirigami-parity-design.md` - this plan implements slice **S1** of its twelve-slice stack. Read the spec's "Current state", "Architecture decisions" and "Testing strategy" sections before starting; the sibling slices (S2 split panes, S3 sidebar, ...) are out of scope here.

## Global Constraints

- The C engine source stays pure ASCII and warning-clean. `LC_ALL=C grep -nP '[^\x00-\x7F]' src/*.c src/*.h` must produce no output, and the build log must contain no `warning:` line (CI fails the build on either).
- All seven required CI check names stay unchanged: `source-checks`, `package-versions`, `selftest`, and the four `build (...)` matrix legs.
- The legacy SDL application keeps building and keeps passing its `--selftest*` suites. Its sources are not modified except where the ABI in `src/core_api.c` is shared with it.
- No engine behaviour is reimplemented in C++ or QML. Scanning, sorting, hidden-entry filtering and folder sizes stay in C; the Qt side only asks for them through `src/core_api.h`.
- Model mutation happens only on the GUI thread. The GUI thread never blocks on engine work.
- The shipped QML renders through Qt Quick's hardware scene graph. No `Canvas`, `QPainter` or `QGraphicsView` in shipped code, and no forced software backend. Offscreen software rendering is test-only.
- Qt 6 and KF6 as shipped by the four CI images; no separate older baseline.
- Build and test inside the container: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && ...'`. The host has no SDL3 development packages, so a host `./build.sh` fails for that reason alone.
- The runtime UI is target-only until it is exercised on a real Plasma session; headless runs verify builds and plumbing only.
- Local commits only. The executor opens the pull request; do not push, tag or rewrite history from a task.

## Review Focus

Five input classes or failure modes this spec implies that no task's tests would otherwise exercise. Each line below is pinned by a test in the task named at its end.

1. The directory is deleted, unmounted or becomes unreadable between the navigation and the scan completing: the previous rows must stay on screen and the failure must surface, not blank the view (Task 2).
2. The same directory is refreshed while a scan of it is still running: the view must show one set of rows, never two, and never a half-updated list (Task 2).
3. Hidden entries (dotfiles) are toggled on while a directory is displayed: entries appear and disappear consistently, and a hidden entry is never reachable by a path that was never listed (Task 3).
4. Folder sizes arrive after the rows are shown: the size column fills in without the list reordering under the user's cursor, and a size that is not yet known is not rendered as zero (Task 3).
5. A file name that is not valid UTF-8 is listed and selected: the page must carry the path the model produced through to the selection unchanged, without re-encoding it. The model decodes an invalid byte sequence to U+FFFD for display but also retains the scanned raw bytes. An operation resolves the displayed path back to those bytes and targets the exact file when the mapping is unique; when two raw names decode to the same displayed path it is refused with a visible message (Task 5).

---

### Task 1: Asynchronous scan handle in the C ABI

**Files:**
- Modify: `src/core_api.h` (add the scan handle, the options struct, the four scan functions, the extra sort keys)
- Modify: `src/core_api.c` (implement them; keep `hermes_core_scan_sorted` working)
- Test: `tools/core-abi-selftest.c`

**Interfaces:**
- Consumes: the engine's `listing_new(path, key, desc, hidden)`, `listing_start(L)`, `listing_release(L)`, `listing_build_order_ex(L, &opts, &out, &count)`, the `LST_COMPLETE` state and the engine sort keys `SORT_NAME, SORT_DATE, SORT_TYPE, SORT_SIZE, SORT_CREATED, SORT_EXT, SORT_PERMS, SORT_OWNER` (`src/fs.c:26`).
- Produces, for Tasks 2-8:

```c
typedef struct HermesScan HermesScan;

typedef struct {
    HermesSortKey key;   // HERMES_SORT_*
    bool desc;           // descending order
    bool show_hidden;    // include dotfiles and other hidden entries
    bool folder_sizes;   // compute recursive sizes for the listed folders
} HermesScanOptions;

// Start a scan on the engine's worker pool. Returns NULL on invalid input.
// The handle owns one reference to the engine listing; the engine keeps the
// listing alive until its own jobs finish, so freeing early is safe.
HermesScan *hermes_core_scan_start(HermesCore *core, const char *path,
                                   const HermesScanOptions *options);

// Non-blocking. Returns 1 and fills `out` when the scan is complete, 0 while
// it is still running, -1 when the scan failed. Calling it again after
// completion refreshes `out` with the current entry metadata.
int hermes_core_scan_poll(HermesCore *core, HermesScan *scan, HermesListing *out);

// Ask a running scan to stop early. Safe on a finished scan.
void hermes_core_scan_cancel(HermesScan *scan);

// Release the handle. Safe while the engine's jobs are still running.
void hermes_core_scan_free(HermesScan *scan);
```

`HermesSortKey` gains `HERMES_SORT_CREATED = 4`, `HERMES_SORT_EXT = 5`, `HERMES_SORT_PERMS = 6`, `HERMES_SORT_OWNER = 7`, in the same order as the engine's enum. `hermes_core_scan_sorted` stays in the header with its current signature and becomes a wrapper that starts a scan with `show_hidden = true`, `folder_sizes = false` and polls it to completion, so `tools/core-abi-selftest.c`, `tools/core-api-probe.cpp` and the existing callers keep working unchanged.

- [ ] **Step 1: Write the failing ABI tests**

In `tools/core-abi-selftest.c` add four test functions, following the file's existing pattern (a helper that creates a temp directory, `ST_CHECK`-style assertions are not used there - the file asserts with plain `assert`-like checks and prints `CORE ABI SELFTEST OK` at the end):

- `scan_start_poll_returns_listing`: create a temp directory with the files `b.txt`, `a.txt` and the hidden file `.secret`; call `hermes_core_scan_start` with `key = HERMES_SORT_NAME`, `desc = false`, `show_hidden = false`, `folder_sizes = false`; poll in a loop (at most 2000 iterations, sleeping 1 ms between polls) until the result is 1 or -1; assert the result is 1, the listing holds exactly two entries, and the first entry's name is `a.txt`.
- `scan_hidden_option_includes_dotfiles`: the same directory with `show_hidden = true`; assert the listing holds three entries and that one of them is `.secret`.
- `scan_supports_every_sort_key`: for each of the eight `HermesSortKey` values, start and poll a scan of a directory with `a.txt`, `b.txt` and the subfolder `sub`; assert the poll returns 1 and the listing is non-empty. The order itself is asserted in Task 3, where the model exposes it.
- `scan_free_is_safe_while_running`: start a scan of a directory holding 2000 files, immediately call `hermes_core_scan_cancel` and `hermes_core_scan_free`, then call `hermes_core_drain(core)`; assert nothing crashes and the core still answers `hermes_core_scan` on a small directory afterwards.

- [ ] **Step 2: Run the ABI suite and watch it fail**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-core-abi-selftest 2>&1 | tail -20'`

Expected: the build fails, because `hermes_core_scan_start`, `hermes_core_scan_poll`, `hermes_core_scan_cancel`, `hermes_core_scan_free` and `HermesScanOptions` do not exist yet.

- [ ] **Step 3: Declare the new ABI in `src/core_api.h`**

Add the declarations above, extend `HermesSortKey` with the four new values, and update the comment above `HermesSortKey`, which currently claims "the headless shell never filters or groups, it lists everything" - it now honours the hidden-entry and folder-size options.

- [ ] **Step 4: Implement the scan handle in `src/core_api.c`**

`struct HermesScan` wraps the engine `Listing *` plus the `HermesScanOptions` the scan was started with. `hermes_core_scan_start` validates the core, the path and the options, calls `listing_new(path, engine_sort_key(options->key), options->desc, options->show_hidden)`, copies the options into the handle and calls `listing_start(L)`.

`hermes_core_scan_poll` reads the listing state without blocking. While the state is below `LST_COMPLETE` it returns 0. On `LST_ERROR` it returns -1. On completion it builds the order with `listing_build_order_ex` using an `OrderOpts` whose `key` is the engine key, `desc` the requested direction, `hidden` the requested hidden-entry flag and `meta` true (so sizes and dates are the final ones), copies the entries into the caller's `HermesListing` exactly as `hermes_core_scan_sorted` does today (each name is `strdup`ed, `flags`, `size` and `mtime` are copied), frees the order array and returns 1. Calling it again after completion must free the previous contents of `out` first, because `hermes_listing_free` is the only owner of the copied names.

`hermes_core_scan_cancel` sets the listing's cancel flag so the engine's jobs stop early. `hermes_core_scan_free` releases the handle's reference with `listing_release`, which is safe while the engine's jobs still hold their own references.

- [ ] **Step 5: Make `hermes_core_scan_sorted` a wrapper**

Replace its body with: start a scan with `HERMES_SORT_NAME`/`desc`/`show_hidden = true`/`folder_sizes = false`, poll until it returns 1 or -1, free the handle, and return the same values the function returns today (0 on success, -1 on failure). Keep the current behaviour of returning -1 when the scan does not complete.

- [ ] **Step 6: Run the ABI suite and the core suite**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-core-abi-selftest hermes-core-selftest hermes-cpp-probe 2>&1 | tee /tmp/t1.log && ./build/cmake-final/hermes-core-abi-selftest && ./build/cmake-final/hermes-core-selftest && ./build/cmake-final/hermes-cpp-probe && ! grep -q "warning:" /tmp/t1.log'`

Expected: `CORE ABI SELFTEST OK`, `CORE SELFTEST OK`, `CPP ABI PROBE OK`, and no `warning:` line in the log.

- [ ] **Step 7: Check the ASCII and whitespace rules**

Run: `LC_ALL=C grep -nP '[^\x00-\x7F]' src/*.c src/*.h; git diff --check`

Expected: no output from either command.

- [ ] **Step 8: Commit**

```bash
git add src/core_api.h src/core_api.c tools/core-abi-selftest.c
git commit -m "feat(core): expose an asynchronous scan handle"
```

---

### Task 2: The directory model scans without blocking the GUI thread

**Files:**
- Modify: `qtui/directory_model.h`, `qtui/directory_model.cpp`
- Modify: `qtui/shell_controller.cpp` (the core wake callback)
- Test: `tools/qtui-selftest.cpp`

**Interfaces:**
- Consumes: the four scan functions from Task 1.
- Produces:

```cpp
// directory_model.h
class DirectoryModel : public QAbstractListModel {
public:
  bool navigate(const QString &path);   // starts the scan, returns immediately
  void refresh();                       // restarts the scan of the current path
  void pollScan();                      // called on the GUI thread when the engine wakes

signals:
  void navigateFailed(const QString &path);
};
```

`pollScan()` is the only place that touches the engine's listing. It is called on the GUI thread and applies the result only when the result belongs to the current path and the current generation. The view learns about new rows through the usual model reset, not through an extra signal.

- [ ] **Step 1: Write the failing Qt tests**

In `tools/qtui-selftest.cpp` add three slots:

- `directoryModelNavigationDoesNotBlock`: create a temporary directory with 5000 files; construct a `DirectoryModel` over a fresh core; call `navigate(dir)`; assert `rowCount()` is still 0 immediately after the call (the scan has not been applied yet, which is only possible if `navigate` did not scan synchronously); then `QTRY_VERIFY_WITH_TIMEOUT(model.rowCount() == 5000, 10000)`.
- `directoryModelAppliesAsyncResults`: navigate to a directory with two files; `QTRY_COMPARE(model.rowCount(), 2)`; assert the first row's name role is the alphabetically first file.
- `directoryModelKeepsRowsWhenTheDirectoryDisappears`: navigate to a temporary directory with one file and wait for the rows; delete the directory; call `refresh()`; assert the model still holds the previous row and that `navigateFailed` was emitted (use a `QSignalSpy`).
- `directoryModelRefreshDoesNotDuplicateRows`: navigate to a directory with two files and wait for the rows; call `refresh()` twice in a row and wait for the scans to be applied; assert the row count is still 2, not 4.

Also update the two existing tests that reach into the model's private seam (`navigationIgnoresStaleDirectoryResults`, `directoryModelRejectsStaleScanResults`, `tools/qtui-selftest.cpp:42` and `:50`): they must keep proving that a result from an older generation is dropped. Keep the `friend class HermesQtUiTest` seam, but the seam method it calls changes name and shape - see Step 3.

- [ ] **Step 2: Run the Qt suite and watch it fail**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-qt-selftest 2>&1 | tail -20'`

Expected: the build fails on the new test slots (the model has no `pollScan` yet) and the existing stale-result tests still compile against the old seam.

- [ ] **Step 3: Rewrite the model around the scan handle**

In `qtui/directory_model.h`, replace the private `applyScan` with:

```cpp
  bool applyScan(const QString &path, quint64 generation,
                 const HermesListing &listing, const QString &previousPath);
```

which applies an already-fetched listing to the model, and add the private members `HermesScan *m_scan` and `QString m_scanPath`.

In `qtui/directory_model.cpp`:

- `navigate(path)` claims the path and bumps the generation as it does today, then starts a scan instead of scanning: it cancels and frees any pending scan, calls `hermes_core_scan_start` with the model's current sort key, direction, hidden flag and folder-size flag, stores the handle and the scanned path, and returns true. It returns false only when the engine refuses to start the scan (invalid path), in which case it restores the previous path and emits `navigateFailed` exactly as today.
- `refresh()` restarts the scan of the current path and keeps the existing rows until the new ones arrive.
- `pollScan()` polls the pending handle. While the engine reports the scan is still running it returns. When the scan completes it calls `applyScan`, frees the handle and emits `scanFinished`. When the scan failed it keeps the previous rows, frees the handle and emits `navigateFailed` for the scanned path. A result whose path or generation no longer matches the model is dropped without touching the rows.
- `applyScan` copies the listing into the model's entries, swaps them in with `beginResetModel`/`endResetModel` and emits `pathChanged` when the path actually changed - the same rule the current code uses, so the selection clearing in `qtui/qml/DirectoryPage.qml:25-37` keeps working.

- [ ] **Step 4: Poll from the engine's wake callback**

In `qtui/shell_controller.cpp` the core wake callback (around line 146) currently calls `controller->m_directory->refresh()`. Change it to call `pollScan()` instead: the wake arrives when the engine published a state change, so the right reaction is to fetch the result, not to start a new scan. Keep the `hermes_core_drain` call.

- [ ] **Step 5: Run the Qt suite**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-qt hermes-qt-selftest 2>&1 | tee /tmp/t2.log && QT_QPA_PLATFORM=offscreen ./build/cmake-final/hermes-qt-selftest 2>&1 | tail -5 && ! grep -q "warning:" /tmp/t2.log'`

Expected: all tests pass, including the three new ones and the two updated stale-result ones. If `directoryModelNavigationDoesNotBlock` fails with a non-zero row count, `navigate` is still scanning synchronously.

- [ ] **Step 6: Run the whole local suite set**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && ./build/cmake-final/hermes-core-selftest && ./build/cmake-final/hermes-core-abi-selftest && ./build/cmake-final/hermes-cpp-probe && sh tools/portal-selftest.sh 2>&1 | tail -2'`

Expected: all three C suites report OK and the portal suite reports 28 passed, 0 failed.

- [ ] **Step 7: Commit**

```bash
git add qtui/directory_model.h qtui/directory_model.cpp qtui/shell_controller.cpp tools/qtui-selftest.cpp
git commit -m "feat(ui): scan directories without blocking the GUI thread"
```

---

### Task 3: Hidden entries, every sort key and folder sizes

**Files:**
- Modify: `src/core_api.h`, `src/core_api.c` (folder-size start)
- Modify: `qtui/directory_model.h`, `qtui/directory_model.cpp`
- Test: `tools/core-abi-selftest.c`, `tools/qtui-selftest.cpp`

**Interfaces:**
- Consumes: `HermesScanOptions` from Task 1; the engine's `dirsize_start(L, visible, hidden, remote, host, port, ...)` (`src/search.c:1298`).
- Produces:

```c
// core_api.h
// Ask the engine to compute the recursive size of every folder in the scan.
// The sizes arrive later; poll the scan again to pick them up.
int hermes_core_scan_start_folder_sizes(HermesCore *core, HermesScan *scan);
```

```cpp
// directory_model.h
  void setShowHidden(bool show);   // restarts the scan
  bool showHidden() const;
  void setFolderSizes(bool on);    // restarts the scan
  bool folderSizes() const;
  void setSort(HermesSortKey key, bool desc);  // explicit direction; restarts the scan
  void setSortKey(HermesSortKey key);          // the legacy app's policy; restarts the scan
```

`setSortKey` reproduces the legacy app's policy for choosing a sort key (`src/app.c:5183-5190`): a new key starts descending for `HERMES_SORT_DATE` and `HERMES_SORT_SIZE` and ascending for every other key, and choosing the key that is already active flips the direction. `setSort(key, desc)` stays the explicit-direction entry point.

The model exposes a `Q_INVOKABLE QStringList sortKeys() const` returning the eight sort keys in engine order, so the QML can build a sort menu without duplicating the list.

- [ ] **Step 1: Write the failing tests**

In `tools/core-abi-selftest.c` add `scan_folder_sizes_start_returns_zero`: start a scan of a directory containing the subfolder `sub` with one file, poll it to completion, call `hermes_core_scan_start_folder_sizes` and assert it returns 0.

In `tools/qtui-selftest.cpp` add:

- `directoryModelHonoursHiddenToggle`: a directory with `visible.txt` and `.hidden.txt`; navigate with hidden off and assert the row count is 1; call `setShowHidden(true)` and `QTRY_COMPARE(rowCount(), 2)`; assert the hidden entry's name role is `.hidden.txt`.
- `directoryModelSortsByEveryKey`: a directory with `a.txt`, `b.txt` and the subfolder `sub`; for each of the eight keys call `setSort(key, false)`, wait for the scan to be applied, and assert the first row's name is the expected one for that key. The expectations come from the engine's sort implementation in `src/fs.c`, so with an ascending sort the date and created keys give **oldest first** (`src/fs.c:260-261`, `:272-274`; `cmp_entries` returns `c->desc ? -r : r`). The fixture must give its files distinct modification times, and distinct creation times where the platform records them, so the order is fixed rather than dependent on when the test ran.
- `directoryModelSortKeyFollowsLegacyPolicy`: choosing a key through `setSortKey` starts descending for the date and size keys and ascending for the others, and choosing the active key again flips the direction.
- `directoryModelReportsFolderSizes`: a directory with the subfolder `sub` holding one file; navigate with `setFolderSizes(true)`; `QTRY_VERIFY` that the subfolder's size role becomes greater than zero. A size that is not known yet must not read as zero-with-known-flag; the model reports an unknown size as -1.

- [ ] **Step 2: Run both suites and watch them fail**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-core-abi-selftest hermes-qt-selftest 2>&1 | tail -20'`

Expected: the build fails, because `hermes_core_scan_start_folder_sizes`, `setShowHidden` and `setFolderSizes` do not exist yet.

- [ ] **Step 3: Add the folder-size entry point to the ABI**

In `src/core_api.c`, `hermes_core_scan_start_folder_sizes` calls the engine's `dirsize_start` for the scan's listing with the visible and hidden folders the options asked for, and returns 0 on success, -1 when the handle is invalid or the scan has already completed. `dirsize_start` takes remote-connection arguments; the headless shell passes the non-remote values.

- [ ] **Step 4: Teach the model the three options**

In `qtui/directory_model.{h,cpp}`: store the hidden flag, the folder-size flag and the sort key; pass them in every `hermes_core_scan_start`; `setShowHidden`, `setFolderSizes` and `setSort` store the value and restart the scan. When folder sizes are on, the model calls `hermes_core_scan_start_folder_sizes` once the scan completes, and `pollScan` keeps polling afterwards so the sizes appear when the engine publishes them.

The size role reports -1 while a folder's size is not known yet, so the view can show a placeholder instead of a wrong zero.

- [ ] **Step 5: Run the suites**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-core-abi-selftest hermes-qt-selftest 2>&1 | tee /tmp/t3.log && ./build/cmake-final/hermes-core-abi-selftest && QT_QPA_PLATFORM=offscreen ./build/cmake-final/hermes-qt-selftest 2>&1 | tail -3 && ! grep -q "warning:" /tmp/t3.log'`

Expected: `CORE ABI SELFTEST OK` and all Qt tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/core_api.h src/core_api.c qtui/directory_model.h qtui/directory_model.cpp tools/core-abi-selftest.c tools/qtui-selftest.cpp
git commit -m "feat(ui): expose hidden entries, sort keys and folder sizes"
```

---

### Task 4: View modes, columns and zoom

**Files:**
- Modify: `qtui/qml/DirectoryPage.qml`
- Modify: `qtui/shell_controller.h`, `qtui/shell_controller.cpp`
- Test: `tools/qtui-selftest.cpp`

**Interfaces:**
- Consumes: the model's sort key and the size role from Task 3.
- Produces, on the controller:

```cpp
  Q_PROPERTY(int viewMode READ viewMode NOTIFY viewModeChanged)   // 0 list, 1 details, 2 grid
  Q_PROPERTY(QVariantList columns READ columns NOTIFY columnsChanged)  // each: {key, width}
  Q_PROPERTY(double zoom READ zoom NOTIFY zoomChanged)             // 0.6 .. 2.5
  Q_INVOKABLE void setViewMode(int mode);
  Q_INVOKABLE void setZoom(double zoom);
```

The column keys are `name`, `date`, `size`. The controller starts with the legacy defaults `name,date:140,size:88` (`src/settings.c:330`). Persistence in the settings file is not part of this plan; it lands with the settings slice (S10).

- [ ] **Step 1: Write the failing Qt test**

Add `directoryViewExposesModesColumnsAndZoom`: construct a `ShellController`, assert `viewMode()` is 0, `columns()` holds three entries whose keys are `name`, `date`, `size`, and `zoom()` is 1.0; call `setViewMode(1)` and `setZoom(1.5)` and assert the properties and their change signals.

- [ ] **Step 2: Run the Qt suite and watch it fail**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-qt-selftest 2>&1 | tail -20'`

Expected: the build fails, because the controller has no `viewMode`, `columns` or `zoom`.

- [ ] **Step 3: Add the controller properties**

In `qtui/shell_controller.{h,cpp}` add the three properties with their change signals and the two invokable setters. `setViewMode` clamps the value to 0..2, `setZoom` clamps it to 0.6..2.5 (the same range as `src/settings.c:194`).

- [ ] **Step 4: Render the three view modes**

In `qtui/qml/DirectoryPage.qml`:

- Wrap the current single-column list in a `ListView` that is visible when `hermes.viewMode === 0` (list) and add a details view that shows the columns `hermes.columns` with their widths, visible when `hermes.viewMode === 1`. The details view is a `ListView` of rows built from a `Repeater` over the column list, each row placing a `Text` with `width: column.width * hermes.zoom`.
- Add a grid view: a `GridView` of tiles, visible when `hermes.viewMode === 2`, each tile showing the icon, the name and the size.
- The row height scales with `hermes.zoom`; the name column takes the remaining width.
- Keep the existing selection and double-click behaviour (`toggleSelection`, `hermes.openEntry`) in all three modes.
- The size text shows a placeholder while the size role is -1.

- [ ] **Step 5: Run the Qt suite**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-qt hermes-qt-selftest 2>&1 | tee /tmp/t4.log && QT_QPA_PLATFORM=offscreen ./build/cmake-final/hermes-qt-selftest 2>&1 | tail -3 && ! grep -q "warning:" /tmp/t4.log'`

Expected: all tests pass, including the new one.

- [ ] **Step 6: Commit**

```bash
git add qtui/shell_controller.h qtui/shell_controller.cpp qtui/qml/DirectoryPage.qml tools/qtui-selftest.cpp
git commit -m "feat(ui): add list, details and grid view modes"
```

---

### Task 5: Multi-selection, the current item and keyboard navigation

**Files:**
- Modify: `qtui/qml/DirectoryPage.qml`
- Test: `tools/qtui-selftest.cpp`

**Interfaces:**
- Consumes: the model's `PathRole` and the page's existing `toggleSelection(path)` and `clearSelection()`.
- Produces, on the page: `property int currentIndex`, `property var selectedPaths` (already there), and the functions `selectRange(int index)`, `setCurrentIndex(int index)`, `activateCurrent()`.

- [ ] **Step 1: Write the failing Qt test**

Add `selectionSupportsRangeAndKeyboard`: load `qrc:/qtui/qml/main.qml` in an offscreen engine exactly as `selectionUsesPathListAndClearsOnNavigation` does, navigate to a directory with three files and one subdirectory, then call the page's `toggleSelection(pathOfFirstFile)` and `selectRange(indexOfLastFile)`; assert the selection holds the four paths from the first file to the last; call `setCurrentIndex(indexOfSubdirectory)` and assert `currentIndex` is that index; call `activateCurrent()` and assert the model navigated into the subdirectory. The fixture must use a directory for the activation, because activating a file opens it through the portal client, which must not run in a test.

- [ ] **Step 2: Run the Qt suite and watch it fail**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-qt-selftest 2>&1 | tail -20'`

Expected: the test fails because the page has no `selectRange`, `setCurrentIndex` or `activateCurrent`.

- [ ] **Step 3: Implement the selection and keyboard behaviour**

In `qtui/qml/DirectoryPage.qml`:

- Keep `selectedPaths` as an array of paths (the PR 3 shape) and add `currentIndex`.
- `selectRange(index)` selects every path from the current index to `index`, replacing the previous range selection.
- `setCurrentIndex(index)` stores the index and scrolls the view to it.
- `activateCurrent()` opens the current entry with `hermes.openEntry(path)`.
- Add a `Keys.onPressed` handler on the list: Up/Down move the current index, Home/End jump to the ends, Space toggles the selection of the current entry, Enter activates it, Ctrl+A selects everything.
- The mouse handlers keep working: a plain click sets the current index, Ctrl+click toggles, Shift+click selects the range.

- [ ] **Step 4: Keep the non-UTF-8 path intact**

The path strings that reach the model come from the C ABI as UTF-8 byte arrays. The page must not re-encode them: keep passing the `PathRole` value through unchanged. The existing test `directoryModelPreservesUtf8Names` covers the model side for valid UTF-8 names; add the selection side to the new test by selecting an entry whose name is not valid UTF-8 and asserting the selected path is exactly the path the model reports.

Note the boundary this test pins: the model decodes an invalid byte sequence to U+FFFD for display but keeps the scanned raw bytes. Operations resolve the displayed path back to those bytes; if two rows decode to the same displayed path, the operation is refused with a visible message instead of guessing which file the user meant. The page still passes `PathRole` through unchanged as the selection key.

- [ ] **Step 5: Run the Qt suite**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-qt hermes-qt-selftest 2>&1 | tee /tmp/t5.log && QT_QPA_PLATFORM=offscreen ./build/cmake-final/hermes-qt-selftest 2>&1 | tail -3 && ! grep -q "warning:" /tmp/t5.log'`

Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add qtui/qml/DirectoryPage.qml tools/qtui-selftest.cpp
git commit -m "feat(ui): add range selection and keyboard navigation"
```

---

### Task 6: Refresh the view when the directory changes

**Files:**
- Create: `qtui/directory_watcher.h`, `qtui/directory_watcher.cpp`
- Modify: `qtui/directory_model.h`, `qtui/directory_model.cpp`
- Modify: `qtui/shell_controller.cpp` (own the watcher)
- Modify: `CMakeLists.txt` (add the new source file to `hermes-kirigami-ui`)
- Test: `tools/qtui-selftest.cpp`

**Interfaces:**
- Consumes: `DirectoryModel::refresh()` from Task 2.
- Produces:

```cpp
// directory_watcher.h
class DirectoryWatcher : public QObject {
  Q_OBJECT
public:
  explicit DirectoryWatcher(QObject *parent = nullptr);
  void watch(const QString &path);
  void unwatch();
signals:
  void changed();   // emitted at most once per quiet period
};
```

The watcher uses `QFileSystemWatcher` on the watched directory and a 300 ms single-shot timer, so a burst of writes triggers one refresh.

- [ ] **Step 1: Write the failing Qt test**

Add `directoryModelRefreshesWhenTheDirectoryChanges`: create a temporary directory with one file; construct a `ShellController`; navigate to it and wait for the row count to be 1; create a second file in the directory; `QTRY_VERIFY_WITH_TIMEOUT` that the row count becomes 2 within 10000 ms.

- [ ] **Step 2: Run the Qt suite and watch it fail**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-qt-selftest 2>&1 | tail -20'`

Expected: the test times out on the row count, because nothing watches the directory yet.

- [ ] **Step 3: Implement the watcher**

`qtui/directory_watcher.{h,cpp}`: `watch(path)` removes any previous watch, adds the path to a `QFileSystemWatcher`, and connects its `directoryChanged` signal to a 300 ms single-shot timer that emits `changed()`. `unwatch()` clears the watch and stops the timer.

- [ ] **Step 4: Wire the watcher to the model**

The controller owns the watcher, watches the current path whenever the current tab's path changes, and calls `m_directory->refresh()` when the watcher fires. The watcher is unwatched when the path changes or the controller is destroyed. `DirectoryModel` itself stays unaware of the watcher, so the model can still be used headless.

- [ ] **Step 5: Add the new file to the build**

In `CMakeLists.txt`, add `qtui/directory_watcher.cpp` to the `hermes-kirigami-ui` sources.

- [ ] **Step 6: Run the Qt suite**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake -S . -B build/cmake-final -DHERMES_BUILD_QT_UI=ON -DHERMES_DEFAULT_UI=legacy && cmake --build build/cmake-final --target hermes-qt hermes-qt-selftest 2>&1 | tee /tmp/t6.log && QT_QPA_PLATFORM=offscreen ./build/cmake-final/hermes-qt-selftest 2>&1 | tail -3 && ! grep -q "warning:" /tmp/t6.log'`

Expected: all tests pass, including the new one.

- [ ] **Step 7: Commit**

```bash
git add qtui/directory_watcher.h qtui/directory_watcher.cpp qtui/directory_model.h qtui/directory_model.cpp qtui/shell_controller.cpp CMakeLists.txt tools/qtui-selftest.cpp
git commit -m "feat(ui): refresh the view when the directory changes"
```

---

### Task 7: Screenshot and record debug surfaces

**Files:**
- Modify: `qtui/main.cpp`
- Test: `tools/qtui-selftest.cpp`

**Interfaces:**
- Consumes: `QQuickWindow::grabWindow()` and the QML window object.
- Produces, on the command line:

```
hermes-qt --screenshot <file> [DIR]
HERMES_RECORD=<dir> hermes-qt [DIR]
```

`--screenshot` renders one frame of the window showing `DIR` and writes it to `<file>`. The format follows the extension, so `.png` and `.bmp` both work. `HERMES_RECORD=<dir>` writes a frame into `<dir>` after every click, with the file names numbered in order. This mirrors the legacy `--screenshot` and `HERMES_RECORD` behaviour (`src/app.c:1078-1094`).

- [ ] **Step 1: Write the failing Qt test**

Add `cliScreenshotWritesAnImage`: run the `hermes-qt` executable next to the test binary with `--screenshot <tmp>/shot.png <tmpdir>` and `QT_QPA_PLATFORM=offscreen`; assert the process exits 0 and that the file exists and is larger than zero bytes. The test must locate the executable the way `cliHelpAndVersionNeedNoPlatformPlugin` does (`QCoreApplication::applicationDirPath() + "/hermes-qt"`).

- [ ] **Step 2: Run the Qt suite and watch it fail**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-qt-selftest 2>&1 | tail -20'`

Expected: the test fails, because `--screenshot` is not handled and the process exits with the usage error.

- [ ] **Step 3: Implement the screenshot mode**

In `qtui/main.cpp`: recognise `--screenshot <file>` before the event loop starts. After the QML is loaded, take the first root object as a `QQuickWindow`, resize it to 1280x800 and show it. The grab must happen after the scene graph has rendered a frame, otherwise the image is empty: connect to the window's `afterRendering` signal, grab the image inside that callback with `QImage image = window->grabWindow();`, write it with `image.save(file)` and quit the event loop with 0. When the path argument is missing, use the home directory.

For `HERMES_RECORD=<dir>`, count the window's mouse presses with an event filter on the window and, after each press, grab the frame the same way and write `<dir>/frame-%04d.png`, numbered in order.

- [ ] **Step 4: Run the Qt suite**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake --build build/cmake-final --target hermes-qt hermes-qt-selftest 2>&1 | tee /tmp/t7.log && QT_QPA_PLATFORM=offscreen ./build/cmake-final/hermes-qt-selftest 2>&1 | tail -3 && ! grep -q "warning:" /tmp/t7.log'`

Expected: all tests pass, and the new test finds a non-empty image file.

- [ ] **Step 5: Commit**

```bash
git add qtui/main.cpp tools/qtui-selftest.cpp
git commit -m "feat(ui): add screenshot and record debug modes"
```

---

### Task 8: Documentation and changelog

**Files:**
- Modify: `docs/building.md`
- Modify: `docs/architecture.md`
- Modify: `CHANGELOG.md`

**Interfaces:**
- Consumes: nothing; documents the state the earlier tasks produced.
- Produces: nothing that later tasks consume.

- [ ] **Step 1: Update the known-limitation note**

`docs/building.md` records the synchronous GUI-thread scan as a known limitation (the note added in `f7bff48`, around lines 133-137). Replace it with the new behaviour: the scan runs on the engine's worker pool, the GUI thread polls the result, and a superseded scan is dropped. Keep the note about the folder-size pass, which still costs time on deep trees.

- [ ] **Step 2: Document the new files and the ABI**

In `docs/architecture.md`, add `qtui/directory_watcher.*` to the "Qt/Kirigami shell" section with its responsibility, and describe the asynchronous scan handle in the "Headless core library" section next to the existing `src/core_api.h` description.

- [ ] **Step 3: Add the changelog entry**

In `CHANGELOG.md`, add an "Unreleased" bullet describing the user-visible result: the directory view no longer freezes while a directory is scanned, and the view gained the list, details and grid modes, the column set, the hidden-file toggle, folder sizes, range selection and keyboard navigation.

- [ ] **Step 4: Record the known limitations of this slice**

In `docs/building.md` (or `docs/architecture.md`), record two limitations so they are visible and carried forward:

- A file name that is not valid UTF-8 is decoded to the replacement character for display, while the model retains the scanned raw bytes. An operation targets that exact file when the displayed path maps to one row, and is refused with a visible message when two raw names decode to the same displayed path.
- The date column relies on `QLocale::dateFormat` and `timeFormat` and the `QLocale::toString(QDate, QString)` overload. The Qt suite runs on all four CI images, so the formats are exercised on each distribution's Qt version; a live Plasma session remains the only place to confirm the rendered result.

- [ ] **Step 5: Verify the docs build**

Run: `distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && python3 tools/build-install-selftest.py && python3 tools/branding-selftest.py && python3 tools/legacy-purge-selftest.py && sh tools/check-release-version.sh'

Expected: all three Python suites pass and the release-version check reports the current version.

- [ ] **Step 6: Commit**

```bash
git add docs/building.md docs/architecture.md CHANGELOG.md
git commit -m "docs(ui): describe asynchronous scanning and the new view modes"
```

---

## Final verification

Run after the last task, before the branch is handed over:

```bash
distrobox enter fedora-build -- sh -c 'cd /var/home/juangiler/Projects/hermes && cmake -S . -B build/cmake-final -DHERMES_BUILD_QT_UI=ON -DHERMES_DEFAULT_UI=legacy && cmake --build build/cmake-final --target hermes-legacy hermes-qt hermes-qt-selftest hermes-core-selftest hermes-core-abi-selftest hermes-cpp-probe hermes-portal-selftest 2>&1 | tee /tmp/final-build.log && ! grep -q "warning:" /tmp/final-build.log && QT_QPA_PLATFORM=offscreen ./build/cmake-final/hermes-qt-selftest && ./build/cmake-final/hermes-core-selftest && ./build/cmake-final/hermes-core-abi-selftest && ./build/cmake-final/hermes-cpp-probe && sh tools/portal-selftest.sh 2>&1 | tail -2 && ./build.sh debug && ./build/debug/hermes --selftest-ops && python3 tools/build-install-selftest.py && python3 tools/branding-selftest.py && python3 tools/legacy-purge-selftest.py'
LC_ALL=C grep -nP '[^\x00-\x7F]' src/*.c src/*.h
git diff --check
git status --short
```

Expected: every suite reports OK, the Qt suite reports no failures, the ASCII grep and `git diff --check` produce no output, and `git status --short` lists nothing but the untracked files that belong to the repository owner.
