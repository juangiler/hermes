# Kirigami parity (step 4 of the Qt/Kirigami port)

Date: 2026-11-19
Status: proposed

## Intent and why

This is PR 4 of the five-step port described in
`2026-11-18-qt-kirigami-port-design.md`. PRs 1-2 landed the C ABI and the
headless portal plumbing; PR 3 landed the first Kirigami shell (window, places
drawer, tab strip, directory list, basic operations). Step 4 brings the
Kirigami frontend to **feature parity** with the legacy SDL application so
that step 5 can delete the SDL sources, the SDL build path and the AppImage
distribution code.

The motivation is unchanged: Hermes becomes a first-class KDE Plasma 6
application. It is a native look, native behaviour and native interoperability
that are wanted, not a different widget set for its own sake.

## Decisions taken with the user (2026-11-19)

1. **The work is a stack of topic PRs, not one large PR.** Each slice is a
   separate branch and pull request with its own review; the legacy SDL UI stays
   the default until the final slice lands.
2. **The visual bar is native styling with layout parity.** The Qt application
   renders with Breeze/Kirigami native widgets and fonts; it reproduces the SDL
   application's panels, columns, arrangement and behaviour, not its pixels.
   Screenshots are compared per page to catch missing or misplaced UI, not to
   chase pixel equality.

Decision 2 resolves a conflict inside the master design, which asks for a
pixel-identical result (`2026-11-18-qt-kirigami-port-design.md:144-149`) while
stating the motive as a native Plasma application and forbidding a port of the
custom atlas renderer. Pixel-identity cannot survive native widgets, so
layout parity is the bar and the comparison is a checklist, not a diff.

## Goals

- Every feature of the legacy SDL application exists in the Kirigami frontend
  with the same behaviour: browsing, search, previews, transfers, devices,
  cloud, git, snapshots, compare, disk use, trash, terminal tabs, open-with,
  settings and rebindable shortcuts.
- The Kirigami frontend never blocks the Qt event loop: scanning, searching,
  previewing and transferring all run off the GUI thread.
- The C engine keeps doing the work. No feature is reimplemented in C++ or QML.
- The legacy SDL application keeps building and passing its self-tests until
  step 5 deletes it.
- All seven required CI checks stay green; no C engine source becomes
  non-ASCII or warning-producing.

## Non-goals

- No Flatpak/Snap/AppImage/sandbox work (AppImage removal is step 5).
- No portal role is taken by default; the FileChooser backend stays opt-in.
- No GNOME-first decisions, no other window manager is actively supported.
- No new identity, app id, config path or package name.
- No pixel-identical reproduction of the SDL rendering.
- No rewrite of C engine algorithms in C++.

## Anti-goals

- A Qt frontend that duplicates engine logic instead of calling it.
- Losing the engine's worker-thread behaviour, cancellation or conflict
  handling while moving features across.
- Shipping a "parity" claim while a legacy feature is silently missing.

## Current state

`hermes-qt` today: a `Kirigami.ApplicationWindow` with a places drawer (home,
root, documents and a few more), a tab strip with basename labels, a
`QAbstractListModel` directory list over the sorted-scan ABI, and
create/rename/trash/delete/copy/paste. 33 offscreen Qt tests. The engine is
reachable only through the narrow ABI in `src/core_api.h` and the host hook
table in `src/core_host.h`.

The legacy application is one 16,674-line `src/app.c` plus ~59k lines of C
engine. Its feature surface, from `docs/architecture.md`:

| Area | Legacy location | Notes |
|---|---|---|
| Sidebar | `src/app.c` panel kinds | 12 kinds: drives, network (system/saved/groups), cloud (rclone remotes), devices, phones, trash, pinned folders |
| Views | `src/app.c` | grid (thumbnails), details, list; column set and order; zoom |
| Split panes | `src/app.c` | right and bottom splits inside a tab, drag-to-edge, open/move into a split, active pane |
| Archive | `src/archive.c` | browse inside an archive, extract here / selection / all, compress to zip or tgz as queued operations |
| Drag and drop | `src/dragout.c`, `src/app.c` | drag files out to other applications (`text/uri-list`), drop files in to open them |
| Search | `src/search.c` | fuzzy scoring, Ctrl+P index, recursive search, in-file search (ripgrep or built-in), folder sizes, inotify auto-refresh |
| Previews | `src/preview.c`, `src/svg_preview.c` | thumbnail worker, `.thumbnailer` fallback, text/folder summaries, source resolution |
| Transfers | `src/ops.c` | queue, progress cards, pause/cancel, undo, grouped conflict dialogs, cross-device moves, copy verification |
| Disk use | `src/diskuse.c` | squarified treemap and sunburst |
| Compare | `src/compare.c` | two-tree diff and sync |
| Snapshots | `src/snapshots.c` | Snapper, Timeshift, ZFS |
| Git | `src/git.c` | status, remotes, last commit, stashes |
| Devices | `src/devices.c`, `src/iphone.c`, `src/mtp.c`, `src/usb.c`, `src/devfs.c` | USB, MTP, phones |
| Cloud | `src/rclone.c` | remotes and their files - **still SDL-only today** |
| Terminal | `src/term.c` | terminal tabs - **still SDL-only today** |
| Open with | `src/apps.c` | desktop entries |
| Settings | `src/settings.c` | `key=value` file, ~7 documented groups |
| Keys | `src/keys.c` | ~74 rebindable shortcuts |
| Trash | `src/ops.c`, `src/app.c` | trash panel, restore |

## The stack

Twelve topic PRs, sequential, each branched from updated `main` and merged
before the next starts. The legacy SDL UI remains the default and remains
fully functional until S12 flips the default.

The count is twelve rather than the eight the master design's one-line
summary implies, because parity also covers split panes, archive browsing and
extraction, terminal tabs and drag-and-drop, which that sentence does not
mention.

### S1 - Asynchronous scanning and directory view parity

The foundation; it retires the known limitation that directory scanning runs on
the GUI thread and can block it for up to ten seconds.

- The C ABI gains an asynchronous scan: a request returns a handle, the engine
  completes it on a worker thread and reports back through a host callback.
  `hermes_core_scan_sorted` stays for headless callers.
- The directory model drives a scan request, ignores results from a superseded
  request (generation guard, already present) and updates rows on the GUI
  thread. Navigation and input stay responsive during a scan.
- View parity for the directory page: grid, details and list modes; column
  set and order; zoom; hidden files; folder sizes; sort keys and direction;
  multi-selection; keyboard navigation.
- Automatic refresh when a folder's contents change (inotify, debounced and
  throttled by scan cost), swapping rows in without a flicker.
- Debug surfaces: the Qt equivalents of `--screenshot` and `HERMES_RECORD`
  (offscreen grab and `QQuickWindow::grabWindow()`), needed by the parity pass
  in S12 and by bug reports.
- Exit criteria: the Qt suite contains a test that the event loop keeps
  processing events while a scan is outstanding, and no test blocks on a scan.

### S2 - Split panes

The behaviour to port is the legacy application's, and the legacy's structure
is a tree of panels, each with its own tab strip - not a tree of panes inside a
tab. The details, and the two pull requests the slice is delivered in, are in
`2026-11-20-kirigami-split-panes-design.md`.

- The window holds a tree of panels, split right or down, at most sixteen of
  them. Every panel owns a stack of tabs and draws its own tab strip, and
  every tab shows one directory.
- The active panel is the one that receives navigation, selection and commands.
- Splitting by menu and by dragging a tab or a folder to an edge, plus "open in
  split" and "move to split".
- The panel tree survives a restart like the legacy session does, through the
  existing settings.
- Exit criteria: a panel splits right and down, a folder opens in a new panel,
  a panel closes when its last tab closes, and the layout is restored from the
  settings file.

### S3 - Sidebar parity

- The C ABI exposes drives, network shares, cloud remotes, devices, phones,
  trash and pinned folders, including the grouping structure the legacy
  sidebar uses.
- `src/rclone.c` moves from SDL-only into the core: it needs UI callbacks that
  the core does not have yet, so this is the largest C-side task in the stack.
- The Qt side replaces the flat places list with a tree model (groups and
  items) rendered in the `GlobalDrawer`; selecting an entry navigates to it.
- Exit criteria: every sidebar entry kind of the legacy application is present
  and navigable, and a C ABI test covers the enumeration of each kind.

### S4 - Preview panel and thumbnails

- The preview panel shows the preview the engine produces, at the source
  resolution when it is known, and omits it rather than guessing.
- Folder-view thumbnails for every image type Hermes can decode (PNG, JPEG,
  ICO, SVG and others) through `QImageReader` and the Qt image plugins, keeping
  the engine's thumbnail cache and `.thumbnailer` fallback for exotic formats.
- Requests are asynchronous, latest-request-wins, and never add a background
  behind transparent images: transparent PNG, ICO and SVG pixels stay
  transparent in both thumbnails and the panel.
- Exit criteria: regression tests cover a transparent PNG, ICO and SVG
  thumbnail plus resolution reporting, asserting no background pixel appears.

### S5 - Transfers

- The transfer queue, its progress cards, pause/cancel, undo and the grouped
  conflict dialog move into the Kirigami frontend.
- The engine keeps the behaviour: cross-device moves, atomic replace through a
  temp file, conflicts found up front and asked about together, cancel removes
  partial files, optional per-copy verification.
- Exit criteria: a copy, a cross-device move, a conflict and a cancel are each
  driven end to end from the Qt UI in the test suite.

### S6 - Search

- Fuzzy search, the Ctrl+P index, recursive search, in-file search (ripgrep
  when installed, otherwise the built-in parallel search) and recursive folder
  sizes.
- Results open in a tab; the search scope settings are honoured.
- Exit criteria: a search run returns ranked results through the ABI and the
  Qt model renders them.

### S7 - Archive support

- An archive opens as a listing the user can browse, exactly as the legacy
  application does, so the same view code serves it.
- Extract here, extract selection and extract all, and compress the selection
  to zip or tgz. Extract and compress run as operations in the transfer queue
  from S5, so they get progress, errors and undo for free.
- Exit criteria: an archive is listed through the ABI, an extraction and a
  compression run as queued operations and produce the expected files.

### S8 - Selection actions, terminal tabs and drag-and-drop

- Open-with from the desktop entry list (`src/apps.c`, already in the core),
  the trash panel with restore, and "open in terminal".
- `src/term.c` moves from SDL-only into the core, the way `src/rclone.c` does
  in S3, because it needs UI callbacks the core does not have yet. A terminal
  tab reports its exit status.
- Drag and drop: dragging files out of the window offers the same targets the
  legacy application offers (`text/uri-list` and the application-specific
  payload), and dropping files in opens them. The Qt side replaces the
  X11/SDL path of `src/dragout.c` with `QDrag` and `QDropEvent`; the C file is
  not ported.
- Exit criteria: the open-with menu lists the desktop entries for a file, a
  trashed file can be restored, a terminal tab reports its exit status, and a
  drag-out offers a `text/uri-list` payload.

### S9 - Panels: git, snapshots, compare and disk use

- A git panel (status, remotes, last commit, stashes), a snapshots panel
  (Snapper, Timeshift, ZFS), a folder-compare view, and the disk-use map.
- The disk-use treemap and sunburst are `QQuickItem` subclasses that build
  `QSGGeometryNode` geometry, so they render through the hardware scene graph
  like the rest of the UI. No `Canvas`, `QPainter` or `QGraphicsView`.
- Exit criteria: each panel loads its data through the ABI and renders; the
  disk-use view produces GPU geometry in the test suite.

### S10 - Settings and keys

- A Kirigami settings page over the existing `src/settings.c` file: the Qt UI
  reads and writes the same `key=value` configuration, so there is no second
  configuration format and the legacy application keeps reading it.
- A shortcut editor over `src/keys.c`; all ~74 shortcuts stay rebindable and
  `docs/keys.md` stays accurate.
- Appearance settings that only exist for the SDL renderer (the backend choice,
  for example) are hidden in the Qt UI, because the Qt application is always
  hardware accelerated.
- Exit criteria: changing a setting in the Qt UI changes the same file the
  legacy application reads, and rebinding a key is visible to both.

### S11 - Portal user interface

- The FileChooser picker: a dedicated D-Bus-activatable mode of the same binary
  with its own Kirigami window, as the master design settles.
- The document-portal drag target, so dragged paths are exported with `Add()`
  alongside the legacy targets from S8.
- The accent colour and icon theme follow the Settings portal.
- Exit criteria: the picker opens from a portal request and returns a
  selection; a drag exports its payload to the document portal.

### S12 - Layout parity, default flip and cleanup

- Per-page screenshot comparison against the legacy application as a
  checklist: same panels, same columns, same arrangement, nothing missing.
  Differences in native widget metrics and fonts are expected and accepted.
- `HERMES_DEFAULT_UI` now defaults to the Kirigami application; the legacy
  application remains buildable and selectable until step 5 removes it.
- Documentation, `CHANGELOG.md`, `docs/architecture.md` and `docs/building.md`
  are updated to describe the parity state.

## Architecture decisions

**The C ABI stays the only channel.** Each slice adds typed functions to
`src/core_api.h` and, where the engine needs to call back, new hooks appended
to `src/core_host.h`. Existing hook order is never changed; the eight-field
table from step 1 grows by appending. No slice re-implements scan, search,
preview, git, snapshot or device logic in C++ or QML.

**Threading.** Engine callbacks arrive off the GUI thread. The pattern
established in PR 3 applies: a mutex-protected controller handle, an atomic
value for state read from any thread, and queued invocation onto the GUI thread
for anything that touches Qt objects. Model mutation happens only on the GUI
thread. No Qt call ever blocks the event loop on engine work.

**Model and controller layer.** `qtui/` gains one `QAbstractListModel` per
domain (directory, search, transfers, git, snapshots, devices, cloud, sidebar
tree) and one controller per domain, each a `QObject` exposed to QML. The
existing `ShellController` keeps its current responsibilities: tabs, per-tab
history, clipboard, conflict policy, operation completion.

**QML structure.** The window stays a `Kirigami.ApplicationWindow`. Navigation
between the directory view, the search page and the panels moves to a
`PageRow`, as the master design specifies. The preview panel is a side pane
beside the directory view. Pages bind to C++ models and controllers only.

**Rendering.** Everything renders through Qt Quick's hardware scene graph.
Custom visualisations use `QQuickItem` and `QSGGeometryNode`. Thumbnails use
`QQuickImageProvider` and `QImageReader`. The shipped QML contains no `Canvas`,
`QPainter` or `QGraphicsView`, and the application never forces
`QT_QUICK_BACKEND=software`; the offscreen software backend is test-only.

**Configuration.** `src/settings.c` and `src/keys.c` stay the single source of
truth. The Qt UI reaches them through new ABI accessors, so both applications
share one configuration file and one keybinding file.

**Legacy coexistence.** The SDL application keeps building through `build.sh`
and keeps passing `--selftest*` suites. CI builds both. `HERMES_DEFAULT_UI`
flips only in S12, and step 5 removes the option and the SDL target.

## Testing strategy

- New ABI functions get cases in `tools/core-abi-selftest.c`, the ABI-only
  suite that links the `hermes_core` archive itself.
- New models, controllers and QML get cases in `tools/qtui-selftest.cpp`,
  including the QML-load and interaction patterns that PR 3 established
  (show the window before driving it).
- The legacy `--selftest*` suites stay green for the whole stack.
- The disk-use view is asserted to produce GPU geometry; the thumbnail tests
  assert transparency and resolution reporting.
- Per-page screenshot comparison happens in S12 as a manual checklist recorded
  in `docs/building.md`; it is not a pixel diff and not a CI gate.

## Constraints

- `main` is PR-only; each slice is a branch, atomic commits, a pull request and
  all seven required CI checks green before the next branch starts.
- C engine source stays pure ASCII and warning-clean.
- KDE Plasma 6.6-6.8 is the target. Verified means builds and headless tests;
  live Plasma behaviour is target-only until tested in a real session.
- Build dependencies live in the `fedora-build` Distrobox, not on the host.
- No commit, push, tag or package-identity change without explicit approval.

## Approaches considered

**One large PR for all of step 4.** Rejected: the diff would cover most of the
legacy application at once, and the review rounds in this project have been
able to find real defects only because each round stayed small enough to
examine.

**A stack of topic PRs (chosen).** Each slice is reviewable on its own, and
the legacy application keeps working throughout. Cost: ten merge cycles and
some cross-slice interface churn that later slices absorb.

**Stacked branches against a shared base branch instead of sequential PRs to
`main`.** Rejected: the repository's documented flow is a branch per topic
merged into `main` before the next starts, and `main` is the only long-lived
branch. Sequential slices keep each pull request independently mergeable.

**Pixel-identical reproduction of the SDL look.** Rejected by the user's
decision on the visual bar; it contradicts the native-look motive and the
non-goal against porting the custom renderer.

## What is left to the builder

Exact ABI signatures and their ownership rules, the per-slice file layout in
`qtui/`, the QML page and dialog structure, the model role names, the test
names per slice, the CI package additions (KF6 components beyond Kirigami, if
a slice needs them), and the C-side shape of the `rclone.c` move into the
core.

## Provenance of decisions

The user chose: a stack of topic PRs over one large PR, and native styling
with layout parity over pixel-identical reproduction. The slice list and
order, the sequential-to-`main` mechanics, the ABI-first rule, the threading
pattern, the model/controller split, the `PageRow` navigation and the
configuration single-source rule are the author's calls, consistent with the
master design and the constraints above.
